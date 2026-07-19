#include <lunar/vmm.h>
#include <lunar/mm.h>
#include <lunar/printk.h>
#include <lunar/percpu.h>
#include <lunar/trace.h>
#include <lunar/irq.h>
#include "internal.h"

/* Look up a page by address and release the page, if it exists */
static void release_page_address(physaddr_t physical) {
	struct page* page;
	int err = get_page_from_address(physical, &page);
	if (err == 0) {
		page_release(page); /* Remove page table ref */
		page_release(page); /* Remove ref from get_page_from_address */
	} else {
		bug(err == -EACCES); /* Page mapped with zero refs, very bad thing!! :D */
	}
}

static int hold_page_address(physaddr_t physical, struct page** page) {
	return get_page_from_address(physical, page); /* Gives page with a ref added */
}

/* Unmap a page, with an optional page argument to release the page without a lookup */
static void unmap_page(pte_t* pagetable, struct page* page, uintptr_t virtual) {
	physaddr_t physical = arch_pagetable_get_physical(pagetable, virtual);
	if (physical) {
		bug(arch_pagetable_unmap(pagetable, virtual) != 0);
		if (page)
			page_release(page);
		else
			release_page_address(physical);
	}
}

/* Unmap several pages, does NOT optimize lookup */
static inline void unmap_pages(pte_t* pagetable, uintptr_t virtual, size_t count) {
	for (size_t i = 0; i < count; i++)
		unmap_page(pagetable, NULL, virtual + i * PAGE_SIZE);
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
static int map_page(pte_t* pagetable, uintptr_t virtual, const struct map_page_arg* arg, pgprot_t prot) {
	physaddr_t physical;
	struct page* page;

	if (arg->use_page) {
		physical = hhdm_physical(page_hhdm_virtual(arg->un.page));
		page = arg->un.page;
		page_hold(page);
	} else {
		physical = arg->un.physaddr;
		int err = hold_page_address(physical, &page);
		if (err) {
			if (err == -EACCES)
				return err;

			/* -ENOMEM means that the physical address is not covered by pfndb, so there is nothing to reference */
			bug(err != -ENOMEM);
			page = NULL;
		}
	}

	int err = arch_pagetable_map(pagetable, virtual, physical, false, prot);
	if (err) {
		if (page)
			page_release(page);
		return err;
	}
	return 0;
}

static int map_pages(pte_t* pagetable, uintptr_t virtual, const struct map_pages_arg* arg, pgprot_t prot) {
	for (size_t mapped_pages = 0; mapped_pages < arg->page_count; mapped_pages++) {
		uintptr_t page_virtual = virtual + mapped_pages * PAGE_SIZE;

		struct map_page_arg map_page_arg;
		map_page_arg.use_page = arg->use_pages;
		if (arg->use_pages) {
			map_page_arg.un.page = arg->un.pages[mapped_pages];
			if (!map_page_arg.un.page)
				continue; /* Guard page */
		} else {
			map_page_arg.un.physaddr = arg->un.physaddr + mapped_pages * PAGE_SIZE;
		}

		int err = map_page(pagetable, virtual + mapped_pages * PAGE_SIZE, &map_page_arg, prot);
		if (err) {
			for (size_t i = 0; i < mapped_pages; i++) {
				page_virtual = virtual + i * PAGE_SIZE;
				if (arg->use_pages)
					unmap_page(pagetable, arg->un.pages[i], page_virtual);
				else
					unmap_page(pagetable, NULL, page_virtual);
			}
			return err;
		}
	}

	return 0;
}

static void protect_pages(pte_t* pagetable, uintptr_t virtual, size_t count, pgprot_t prot) {
	for (size_t i = 0; i < count; i++) {
		const uintptr_t page_virtual = virtual + i * PAGE_SIZE;
		const physaddr_t physical = arch_pagetable_get_physical(pagetable, page_virtual);
		if (!physical)
			continue;

		bug(arch_pagetable_update(pagetable, page_virtual, physical, false, prot) != 0);
	}
}

static void force_vma_unmap_enomem(struct mm* mm, uintptr_t virtual, size_t page_count) {
	while (1) {
		int err = vma_unmap(mm, virtual, page_count * PAGE_SIZE);
		if (likely(err == 0))
			break;
		bug(err != -ENOMEM);
		out_of_memory();
	}
}

struct mm* current_mm(void) {
	unsigned long flags = local_irq_save();
	struct mm* ret = current_cpu()->mm_struct;
	local_irq_restore(flags);
	return ret;
}

void* vm_map(void* hint, struct page** pages, size_t page_count, pgprot_t prot, int flags) {
	if (pages == NULL || page_count == 0)
		return ERR_PTR(-EINVAL);
	if (flags & VMM_SEALED)
		return ERR_PTR(-EINVAL);

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	uintptr_t virtual;
	int err = vma_map(mm, (uintptr_t)hint, page_count * PAGE_SIZE, prot, flags, &virtual);
	if (err)
		goto out;

	/* Leave guard pages unmapped */
	for (size_t i = 0; i < page_count; i++) {
		if (pages[i])
			continue;
		const uintptr_t page_virtual = virtual + i * PAGE_SIZE;
		err = vma_protect(mm, page_virtual, PAGE_SIZE, PGPROT_NONE);
		if (err) {
			force_vma_unmap_enomem(mm, virtual, page_count);
			goto out;
		}
	}

	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE))
		unmap_pages(mm->pagetable, virtual, page_count);

	const struct map_pages_arg arg = { .page_count = page_count, .use_pages = true, .un.pages = pages };
	err = map_pages(mm->pagetable, virtual, &arg, prot);
	if (unlikely(err))
		force_vma_unmap_enomem(mm, virtual, page_count);

	tlb_invalidate(virtual, page_count * PAGE_SIZE); /* Invalidate even on error just in case */
out:
	mutex_release(&mm->mutex);
	return (err == 0) ? (void*)virtual : ERR_PTR(err);
}

void* vm_map_physical(void* hint, physaddr_t physical, size_t page_count, pgprot_t prot, int flags) {
	if (physical % PAGE_SIZE != 0 || page_count == 0)
		return ERR_PTR(-EINVAL);

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	uintptr_t virtual;
	int err = vma_map(mm, (uintptr_t)hint, page_count * PAGE_SIZE, prot, flags, &virtual);
	if (err == 0) {
		if (flags & VMM_FIXED && (!(flags & VMM_NOREPLACE)))
			unmap_pages(mm->pagetable, virtual, page_count);

		const struct map_pages_arg arg = { .page_count = page_count, .use_pages = false, .un.physaddr = physical };
		err = map_pages(mm->pagetable, virtual, &arg, prot);
		if (unlikely(err))
			force_vma_unmap_enomem(mm, virtual, page_count);

		tlb_invalidate(virtual, page_count * PAGE_SIZE);
	}

	mutex_release(&mm->mutex);
	return (err == 0) ? (void*)virtual : ERR_PTR(err);
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
		protect_pages(mm->pagetable, (uintptr_t)virtual, page_count, prot);
		tlb_invalidate((uintptr_t)virtual, page_count * PAGE_SIZE);
	}

	mutex_release(&mm->mutex);
	return err;
}

int vm_unmap(void* virtual, size_t page_count, int flags) {
	(void)flags;
	if (page_count == 0)
		return 0;
	if (virtual == NULL)
		return -EINVAL;

	struct mm* mm = current_mm();
	mutex_acquire(&mm->mutex);

	int err = vma_unmap(mm, (uintptr_t)virtual, page_count * PAGE_SIZE);
	if (err == 0) {
		unmap_pages(mm->pagetable, (uintptr_t)virtual, page_count);
		tlb_invalidate((uintptr_t)virtual, page_count * PAGE_SIZE);
	}

	mutex_release(&mm->mutex);
	return err;
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

static void vunmap_node(struct vmalloc_node* node) {
	const size_t unmap_page_count = node->page_count + node->guard_page_count;
	int err = vm_unmap(node->address, unmap_page_count, 0);
	if (err) {
		bug(err != -ENOMEM);
		printk(PRINTK_WARN "mm: %s() vm_unmap() failed, retrying\n", __func__);
		while (1) {
			out_of_memory();
			err = vm_unmap(node->address, unmap_page_count, 0);
			if (!err)
				break;
			bug(err != -ENOMEM);
		}
	}
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

	vunmap_node(node);
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

	vunmap_node(node);
	kfree(node);
}
