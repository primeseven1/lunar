#include <lunar/vmm.h>
#include <lunar/mm.h>
#include <lunar/printk.h>
#include <lunar/percpu.h>
#include <lunar/trace.h>
#include <lunar/irq.h>
#include "internal.h"

/* Look up a page by address and add a reference to it if it exists */
static int hold_page_address(physaddr_t physical, struct page** page, int flags) {
	struct page* tmp;
	int err = get_page_from_address(physical, &tmp); /* Gives page with a ref added */
	if (err == 0) {
		if (flags & VMM_IOMEM && !(tmp->flags & PAGE_FLAG_RESERVED)) {
			page_release(tmp);
			err = -EACCES;
		} else {
			*page = tmp;
		}
	}
	return err;
}

/* Unmap a page, with an optional page argument to release the page without a lookup */
static void unmap_page(struct tlb_batch* batch, struct page* page, uintptr_t virtual) {
	physaddr_t physical = arch_pagetable_get_physical(batch->pagetable, virtual);
	if (!physical)
		return;

	bug(arch_pagetable_unmap(batch->pagetable, virtual) != 0);
	if (!page) {
		int err = get_page_from_address(physical, &page);
		if (err != 0)
			bug(err == -EACCES); /* Mapped with zero refs, very bad thing!! :D */
		else
			page_release(page); /* Drop lookup ref */
	}

	tlb_batch_add(batch, virtual, page);
}

/* Unmap several pages, does NOT optimize lookup */
static inline void unmap_pages(struct tlb_batch* batch, uintptr_t virtual, size_t count) {
	for (size_t i = 0; i < count; i++)
		unmap_page(batch, NULL, virtual + i * PAGE_SIZE);
}

struct map_page_arg {
	bool use_page;
	union {
		struct page* page;
		physaddr_t physaddr;
	} un;
};

struct map_pages_arg {
	size_t page_count;
	bool use_pages;
	union {
		struct page** pages;
		physaddr_t physaddr;
	} un;
};

/*
 * Map a page, either a direct physical address or a struct page*, if mapping a physical address,
 * it attempts to hold the page associated with the address if it exists
 */
static int map_page(struct tlb_batch* batch, uintptr_t virtual, const struct map_page_arg* arg, pgprot_t prot, int flags) {
	struct page* page;
	physaddr_t physical;

	if (arg->use_page) {
		page = arg->un.page;
		physical = hhdm_physical(page_hhdm_virtual(page));
		page_hold(page);
	} else {
		physical = arg->un.physaddr;
		const int err = hold_page_address(physical, &page, flags);
		if (err) {
			if (err == -EACCES)
				return err;

			/* -ENOMEM means that the physical address is not covered by pfndb, so there is nothing to reference */
			bug(err != -ENOMEM);
			page = NULL;
		}
	}

	const int err = arch_pagetable_map(batch->pagetable, virtual, physical, false, prot);
	if (err) {
		if (page)
			page_release(page);
		return err;
	}

	/* Invalidate just in case */
	tlb_batch_add(batch, virtual, NULL);
	return 0;
}

static int map_pages(struct tlb_batch* batch, uintptr_t virtual, const struct map_pages_arg* arg, pgprot_t prot, int flags) {
	for (size_t mapped_pages = 0; mapped_pages < arg->page_count; mapped_pages++) {
		struct map_page_arg map_page_arg;
		map_page_arg.use_page = arg->use_pages;
		if (arg->use_pages) {
			map_page_arg.un.page = arg->un.pages[mapped_pages];
			if (!map_page_arg.un.page)
				continue; /* Guard page */
		} else {
			map_page_arg.un.physaddr = arg->un.physaddr + mapped_pages * PAGE_SIZE;
		}

		const int err = map_page(batch, virtual + mapped_pages * PAGE_SIZE, &map_page_arg, prot, flags);
		if (err) {
			for (size_t i = 0; i < mapped_pages; i++) {
				const uintptr_t page_virtual = virtual + i * PAGE_SIZE;
				if (arg->use_pages)
					unmap_page(batch, arg->un.pages[i], page_virtual);
				else
					unmap_page(batch, NULL, page_virtual);
			}
			return err;
		}
	}

	return 0;
}

static void protect_pages(struct tlb_batch* batch, uintptr_t virtual, size_t count, pgprot_t prot) {
	for (size_t i = 0; i < count; i++) {
		const uintptr_t page_virtual = virtual + i * PAGE_SIZE;
		const physaddr_t physical = arch_pagetable_get_physical(batch->pagetable, page_virtual);
		if (!physical)
			continue;

		bug(arch_pagetable_update(batch->pagetable, page_virtual, physical, false, prot) != 0);
		tlb_batch_add(batch, page_virtual, NULL);
	}
}

static void vma_unmap_force(struct mm* mm, uintptr_t virtual, size_t page_count) {
	int err;
	do {
		err = vma_unmap(mm, virtual, page_count * PAGE_SIZE);
		if (err == -ENOMEM)
			out_of_memory();
	} while (err == -ENOMEM);

	if (err)
		panic("%s() failed: %d", __func__, err);
}

struct mm* current_mm(void) {
	unsigned long flags = local_irq_save();
	struct mm* ret = current_cpu()->mm_struct;
	local_irq_restore(flags);
	return ret;
}

static int check_vm_args_generic(void* hint, size_t page_count, int flags) {
	return (page_count == 0 || (flags & VMM_FIXED && (uintptr_t)hint % PAGE_SIZE != 0) || flags & VMM_SEALED) ? -EINVAL : 0;
}

void* vm_map(void* hint, struct page** pages, size_t page_count, pgprot_t prot, int flags) {
	if (pages == NULL)
		return ERR_PTR(-EINVAL);
	int err = check_vm_args_generic(hint, page_count, flags);
	if (err)
		return ERR_PTR(err);

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	struct tlb_batch tlb_batch;
	tlb_batch_init(&tlb_batch, mm->pagetable);

	uintptr_t virtual;
	err = vma_map(mm, (uintptr_t)hint, page_count * PAGE_SIZE, prot, flags, &virtual);
	if (err)
		goto out;

	/* Leave guard pages unmapped */
	for (size_t i = 0; i < page_count; i++) {
		if (pages[i])
			continue;
		const uintptr_t page_virtual = virtual + i * PAGE_SIZE;
		err = vma_protect(mm, page_virtual, PAGE_SIZE, PGPROT_NONE);
		if (err) {
			vma_unmap_force(mm, virtual, page_count);
			goto out;
		}
	}

	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE)) {
		unmap_pages(&tlb_batch, virtual, page_count);
		tlb_batch_flush(&tlb_batch);
	}

	const struct map_pages_arg arg = { .page_count = page_count, .use_pages = true, .un.pages = pages };
	err = map_pages(&tlb_batch, virtual, &arg, prot, flags);
	if (unlikely(err))
		vma_unmap_force(mm, virtual, page_count);

	tlb_batch_flush(&tlb_batch);
out:
	mutex_release(&mm->mutex);
	return (err == 0) ? (void*)virtual : ERR_PTR(err);
}

static int __vm_map_physical(void* hint, physaddr_t physical, size_t page_count, pgprot_t prot, int flags, void** out) {
	if (physical % PAGE_SIZE != 0)
		return -EINVAL;
	int err = check_vm_args_generic(hint, page_count, flags);
	if (err)
		return err;

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	uintptr_t virtual;
	err = vma_map(mm, (uintptr_t)hint, page_count * PAGE_SIZE, prot, flags, &virtual);
	if (err == 0) {
		struct tlb_batch tlb_batch;
		tlb_batch_init(&tlb_batch, mm->pagetable);
		if (flags & VMM_FIXED && (!(flags & VMM_NOREPLACE))) {
			unmap_pages(&tlb_batch, virtual, page_count);
			tlb_batch_flush(&tlb_batch);
		}

		const struct map_pages_arg arg = { .page_count = page_count, .use_pages = false, .un.physaddr = physical };
		err = map_pages(&tlb_batch, virtual, &arg, prot, flags);
		if (unlikely(err))
			vma_unmap_force(mm, virtual, page_count);

		tlb_batch_flush(&tlb_batch);
	}

	mutex_release(&mm->mutex);

	if (err == 0)
		*out = (void*)virtual;
	return err;
}

void* vm_map_physical(void* hint, physaddr_t physical, size_t page_count, pgprot_t prot, int flags) {
	if (flags & VMM_IOMEM)
		return ERR_PTR(-EINVAL);
	void* ret;
	int err = __vm_map_physical(hint, physical, page_count, prot, flags, &ret);
	return err ? ERR_PTR(err) : ret;
}

int vm_protect(void* virtual, size_t page_count, pgprot_t prot, int flags) {
	(void)flags;
	if (page_count == 0)
		return 0;
	if (virtual == NULL || (uintptr_t)virtual % PAGE_SIZE != 0)
		return -EINVAL;

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	int err = vma_protect(mm, (uintptr_t)virtual, page_count * PAGE_SIZE, prot);
	if (err == 0) {
		struct tlb_batch tlb_batch;
		tlb_batch_init(&tlb_batch, mm->pagetable);
		protect_pages(&tlb_batch, (uintptr_t)virtual, page_count, prot);
		tlb_batch_flush(&tlb_batch);
	}

	mutex_release(&mm->mutex);
	return err;
}

int vm_unmap(void* virtual, size_t page_count, int flags) {
	(void)flags;
	if (page_count == 0)
		return 0;
	if (virtual == NULL || (uintptr_t)virtual % PAGE_SIZE != 0)
		return -EINVAL;

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	int err = vma_unmap(mm, (uintptr_t)virtual, page_count * PAGE_SIZE);
	if (err == 0) {
		struct tlb_batch tlb_batch;
		tlb_batch_init(&tlb_batch, mm->pagetable);
		unmap_pages(&tlb_batch, (uintptr_t)virtual, page_count);
		tlb_batch_flush(&tlb_batch);
	}

	mutex_release(&mm->mutex);
	return err;
}

void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t cache) {
	cache &= PGPROT_PWT | PGPROT_PCD;

	const size_t page_offset = physical % PAGE_SIZE;
	size = ROUND_UP(size + page_offset, PAGE_SIZE);

	void* ret;
	int err = __vm_map_physical(NULL, physical, size >> PAGE_SHIFT, PGPROT_READ | PGPROT_WRITE | cache, VMM_IOMEM, &ret);
	if (err)
		return (void __iomem*)ERR_PTR(err);
	return (u8 __iomem*)ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	const size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	size = ROUND_UP(size + page_offset, PAGE_SIZE);
	return vm_unmap((u8 __force*)virtual - page_offset, size >> PAGE_SHIFT, 0);
}

void vm_unmap_force(void* virtual, size_t page_count, int flags) {
	int err;
	do {
		err = vm_unmap(virtual, page_count, flags);
		if (err == -ENOMEM)
			out_of_memory();
	} while (err == -ENOMEM);

	if (err != 0)
		panic("%s() failed: %d\n", __func__, err);
}

struct vmalloc_node {
	void* address;
	size_t page_count, guard_page_count;
	struct list_node link;
};

static LIST_HEAD_DEFINE(vmalloc_list);
static MUTEX_DEFINE(vmalloc_list_mtx);

void* vmalloc(size_t size) {
	if (size >= SIZE_MAX - PAGE_SIZE)
		return NULL;
	size = ROUND_UP(size, PAGE_SIZE);

	const size_t guard_page_count = 1;
	const size_t page_count = size >> PAGE_SHIFT;
	if (page_count == 0)
		return NULL;

	struct page** const pages = kzalloc((page_count + guard_page_count) * sizeof(*pages), MM_ZONE_NORMAL);
	if (!pages)
		return NULL;

	void* ret = NULL;
	struct vmalloc_node* node = kmalloc(sizeof(*node), MM_ZONE_NORMAL);
	if (!node)
		goto out;
	for (size_t i = 0; i < page_count; i++) {
		pages[i] = page_alloc_page(MM_ZONE_NORMAL);
		if (!pages[i])
			goto out;
	}

	/* When vm_map() encounters null on the last page, it will just reserve the VA with no permissions */
	ret = vm_map(NULL, pages, page_count + guard_page_count, PGPROT_READ | PGPROT_WRITE, 0);
	if (IS_PTR_ERR(ret)) {
		ret = NULL;
		goto out;
	}

	node->address = ret;
	node->page_count = page_count;
	node->guard_page_count = guard_page_count;
	list_node_init(&node->link);

	mutex_acquire(&vmalloc_list_mtx);
	list_add_tail(&vmalloc_list, &node->link);
	mutex_release(&vmalloc_list_mtx);

	node = NULL; /* Prevent kfree() from freeing the node on success */
out:
	/* Now drop this function's ref to the pages, on failure these pages will be released back to the allocator */
	for (size_t i = 0; i < page_count && pages[i] != NULL; i++)
		page_release(pages[i]);

	kfree(node);
	kfree(pages);

	return ret;
}

static inline struct vmalloc_node* get_node_and_unlink(void* ptr) {
	struct vmalloc_node* node = NULL;
	mutex_acquire(&vmalloc_list_mtx);

	struct list_node* tmp;
	list_for_each(tmp, &vmalloc_list) {
		struct vmalloc_node* n = list_entry(tmp, struct vmalloc_node, link);
		if (n->address == ptr) {
			node = n;
			break;
		}
	}

	if (node)
		list_remove(&node->link);

	mutex_release(&vmalloc_list_mtx);
	return node;
}

void* vrealloc(void* ptr, size_t size) {
	if (!ptr)
		return vmalloc(size);
	if (size >= SIZE_MAX - PAGE_SIZE || size == 0)
		return NULL;

	struct vmalloc_node* const node = get_node_and_unlink(ptr);
	if (!node) {
		dump_stack();
		printk(PRINTK_ERR "mm: %s() invalid address\n", __func__);
		return NULL;
	}

	size = ROUND_UP(size, PAGE_SIZE);
	void* const ret = vmalloc(size);
	if (!ret) {
		mutex_acquire(&vmalloc_list_mtx);
		list_add_tail(&vmalloc_list, &node->link);
		mutex_release(&vmalloc_list_mtx);
		return NULL;
	}

	const size_t page_count = size >> PAGE_SHIFT;
	const size_t copy_count = (node->page_count > page_count) ? page_count << PAGE_SHIFT : node->page_count << PAGE_SHIFT;
	memcpy(ret, ptr, copy_count);

	vm_unmap_force(node->address, node->page_count + node->guard_page_count, 0);
	kfree(node);
	return ret;
}

void vfree(void* ptr) {
	if (!ptr)
		return;

	struct vmalloc_node* const node = get_node_and_unlink(ptr);
	if (!node) {
		dump_stack();
		printk(PRINTK_ERR "mm: %s() invalid address\n", __func__);
		return;
	}

	vm_unmap_force(node->address, node->page_count + node->guard_page_count, 0);
	kfree(node);
}
