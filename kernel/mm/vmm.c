#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/ctl.h>
#include <crescent/core/cpu.h>
#include <crescent/core/locking.h>
#include <crescent/core/limine.h>
#include <crescent/core/panic.h>
#include <crescent/core/trace.h>
#include <crescent/core/printk.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vmm.h>
#include <crescent/mm/vma.h>
#include <crescent/lib/string.h>
#include "hhdm.h"
#include "pagetable.h"

static unsigned long mmu_to_pt(mmuflags_t mmu_flags) {
	if (mmu_flags & MMU_CACHE_DISABLE && mmu_flags & MMU_WRITETHROUGH)
		return ULONG_MAX;

	unsigned long pt_flags = 0;
	if (mmu_flags & MMU_READ)
		pt_flags |= PT_PRESENT;
	if (mmu_flags & MMU_WRITE)
		pt_flags |= PT_READ_WRITE;
	if (mmu_flags & MMU_USER)
		pt_flags |= PT_USER_SUPERVISOR;

	if (mmu_flags & MMU_WRITETHROUGH)
		pt_flags |= PT_WRITETHROUGH;
	else if (mmu_flags & MMU_CACHE_DISABLE)
		pt_flags |= PT_CACHE_DISABLE;

	if (!(mmu_flags & MMU_EXEC))
		pt_flags |= PT_NX;

	return pt_flags;
}

static inline bool vmap_validate_flags(int flags) {
	if ((flags & VMM_IOMEM && flags & VMM_ALLOC) ||
			(flags & VMM_PHYSICAL && flags & VMM_ALLOC) ||
			(flags & VMM_NOREPLACE && !(flags & VMM_FIXED)))
		return false;

	return true;
}

static inline bool vprotect_validate_flags(int flags) {
	return flags == 0;
}

static inline bool vunmap_validate_flags(int flags) {
	return flags == 0;
}

static struct mm kernel_mm_struct;

/* Called by vmap, this function checks to see what caused the error, and tries to correct it if possible. */
static bool handle_pagetable_error(pte_t* pagetable, 
		void* virtual, physaddr_t physical, unsigned long pt_flags, 
		int err, int flags) {
	if (err == 0)
		return true;

	/* Simply update the mapping if we want a fixed mapping */
	if (err == -EEXIST && flags & VMM_FIXED) {
		assert(!(flags & VMM_NOREPLACE));
		err = pagetable_update(pagetable, virtual, physical, pt_flags);

		if (err == -EFAULT) {
			if (pt_flags & PT_HUGEPAGE) { /* Putting a 2MiB page where 4K pages are */
				for (unsigned long i = 0; i < PTE_COUNT; i++) {
					pagetable_unmap(pagetable, (u8*)virtual + (i * PAGE_SIZE));
					assert(err == 0 || err == -ENOENT);
				}
				err = pagetable_map(pagetable, virtual, physical, pt_flags);
			} else { /* Putting a 4K page where a 2MiB page is */
				assert(pagetable_unmap(pagetable, virtual) == 0);
				err = pagetable_map(pagetable, virtual, physical, pt_flags);
			}
		}
		if (err)
			return false;
		return true;
	}

	return false;
}

struct page {
	void* start;
	physaddr_t physical;
	size_t page_size;
	mmuflags_t mmu_flags;
	int vmm_flags;
	struct page* next;
};

/* Called by record_page_struct */
static int create_page_struct(struct page** head, void* virtual,
		physaddr_t physical, size_t page_size, mmuflags_t mmu_flags, int vmm_flags) {
	physaddr_t _page = alloc_pages(MM_ZONE_NORMAL, get_order(sizeof(struct page)));
	if (!_page)
		return -ENOMEM;

	struct page* page = hhdm_virtual(_page);

	page->start = virtual;
	page->physical = physical;
	page->page_size = page_size;
	page->mmu_flags = mmu_flags;
	page->vmm_flags = vmm_flags;

	page->next = *head;
	*head = page;

	return 0;
}

/* Called by vmap, can be called when the vma list lock is released */
static inline void free_page_structs(struct page* head) {
	while (head) {
		struct page* next = head->next;
		free_pages(hhdm_physical(head), get_order(sizeof(*head)));
		head = next;
	}
}

/* Must be called with the vma list lock acquired, called by vmap */
static int record_page_struct(struct page** head, struct mm* mm, void* virtual) {
	struct vma* vma = vma_find(mm, virtual);
	if (!vma) /* fine */
		return 0;

	size_t page_size = vma->flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	physaddr_t physical = pagetable_get_physical(mm->pagetable, virtual);

	/* Make sure the page isn't already in the list */
	uintptr_t virtual_start = (uintptr_t)virtual;
	uintptr_t virtual_end = virtual_start + page_size;
	for (struct page* current = *head; current; current = current->next) {
		uintptr_t current_start = (uintptr_t)current->start;
		uintptr_t current_end = current_start + current->page_size;

		/* also also fine */
		if (!(virtual_end <= current_start || virtual_start >= current_end))
			return 0;
	}

	return create_page_struct(head, virtual, physical, page_size, vma->prot, vma->flags);
}

/* Can be called with the lock released, called by vmap */
static void fixed_free_previous(struct page* head) {
	while (head) {
		if (head->vmm_flags & VMM_ALLOC) {
			unsigned int order = get_order(head->page_size);
			free_pages(head->physical, order);
		}

		head = head->next;
	}
}

/* Must be called with the lock acquired, called by vmap when a fixed mapping fails */
static void fixed_restore_previous_after_fail(struct page* head, pte_t* pagetable) {
	while (head) {
		void* _unused;

		/*
		 * Use the noreplace flag here, since we already know there is no VMA there.
		 * Using noreplace can be used for a bug check.
		 */
		int err = vma_map(&kernel_mm_struct, head->start, head->page_size, 
				head->mmu_flags, head->vmm_flags | VMM_FIXED | VMM_NOREPLACE, 
				&_unused);
		assert(err == 0);

		/* Now just remap the page */
		unsigned long pt_flags = mmu_to_pt(head->mmu_flags);
		if (head->vmm_flags & VMM_HUGEPAGE_2M)
			pt_flags |= PT_HUGEPAGE;
		err = pagetable_map(pagetable, head->start, head->physical, pt_flags);
		assert(err == 0);
		tlb_flush_range(head->start, head->page_size);

		head = head->next;
	}
}

void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if (!vmap_validate_flags(flags))
		return NULL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return NULL;

	const size_t page_size = flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	unsigned long page_count = (size + page_size - 1) / page_size;
	if (page_count == 0)
		return NULL;
	const unsigned int page_size_order = get_order(page_size);

	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;
	if (flags & VMM_HUGEPAGE_2M)
		pt_flags |= PT_HUGEPAGE;

	pte_t* pagetable = kernel_mm_struct.pagetable;
	struct page* prev_pages = NULL; /* For fixed mappings, keeps track of the old pages */

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	unsigned long mapped = 0;

	/* Record any pages that could be overwritten, so that we can restore them later if a fixed mapping fails. */
	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE)) {
		u8* virtual = hint;
		u8* end = virtual + size;
		while (virtual < end) {
			int err = record_page_struct(&prev_pages, &kernel_mm_struct, virtual);
			if (err)
				goto cleanup_novma;
			virtual += PAGE_SIZE;
		}
	}

	u8* virtual;
	int err = vma_map(&kernel_mm_struct, hint, size, mmu_flags, flags, (void**)&virtual);
	if (err)
		goto cleanup_novma;
	u8* const ret = virtual;

	if (flags & VMM_PHYSICAL) {
		if (!optional)
			goto cleanup_all;
		physaddr_t physical = *(physaddr_t*)optional;
		if (physical % page_size) {
			err = -EINVAL;
			goto cleanup_all;
		}

		while (page_count--) {
			err = pagetable_map(pagetable, virtual, physical, pt_flags);
			if (!handle_pagetable_error(pagetable, virtual, physical, pt_flags, err, flags))
				goto cleanup_all;

			tlb_flush_range(virtual, page_size);

			mapped++;
			virtual += page_size;
			physical += page_size;
		}
	} else if (flags & VMM_ALLOC) {
		mm_t mm_flags = MM_ZONE_NORMAL;
		if (optional)
			mm_flags = *(mm_t*)optional;

		while (page_count--) {
			physaddr_t physical = alloc_pages(mm_flags, page_size_order);
			if (!physical)
				goto cleanup_all;
			err = pagetable_map(pagetable, virtual, physical, pt_flags | PT_READ_WRITE);
			if (!handle_pagetable_error(pagetable, virtual, physical, pt_flags, err, flags)) {
				free_pages(physical, page_size_order);
				goto cleanup_all;
			}

			tlb_flush_range(virtual, page_size);
			memset(virtual, 0, page_size);
			if (!(pt_flags & PT_READ_WRITE)) {
				assert(pagetable_update(pagetable, virtual, physical, pt_flags) == 0);
				tlb_flush_range(virtual, page_size);
			}

			mapped++;
			virtual += page_size;
		}
	} else {
		goto cleanup_all;
	}

	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);

	/* 
	 * Free previous physical pages allocated with the VMM_ALLOC flag for fixed mappings.
	 * Both of these functions can be called with null pointers, so no need to check for
	 * null here.
	 */
	fixed_free_previous(prev_pages);
	free_page_structs(prev_pages);
	return ret;
cleanup_all:
	while (mapped--) {
		virtual -= page_size;

		physaddr_t physical = 0;
		if (flags & VMM_ALLOC)
			physical = pagetable_get_physical(pagetable, virtual);

		err = pagetable_unmap(pagetable, virtual);
		tlb_flush_range(virtual, page_size);
		if (err)
			printk(PRINTK_ERR "mm: pagetable_unmap failed in cleanup of %s (err: %i)\n", __func__, err);

		/* 
		 * This isn't done in the flags & VMM_ALLOC since we want the reference 
		 * to the physical address to be gone in the page tables including in the TLB 
		 */
		if (physical && err == 0)
			free_pages(physical, page_size_order);
	}

	err = vma_unmap(&kernel_mm_struct, ret, size);
	if (err)
		printk(PRINTK_ERR "mm: vma_unmap failed in error handling of %s (err: %i)\n", __func__, err);

	/* No need to check if prev_pages is null, since the function handles that for us */
	fixed_restore_previous_after_fail(prev_pages, pagetable);
cleanup_novma:
	free_page_structs(prev_pages);
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return NULL;
}

int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags) {
	if ((uintptr_t)virtual % PAGE_SIZE || size == 0 || !vprotect_validate_flags(flags))
		return -EINVAL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return -EINVAL;

	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	int err = 0;

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	void* const end = (u8*)virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(&kernel_mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto out;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->flags & VMM_HUGEPAGE_2M) {
			page_size = HUGEPAGE_2M_SIZE;
			pt_flags |= PT_HUGEPAGE;
		}

		err = vma_protect(&kernel_mm_struct, virtual, page_size, mmu_flags);
		if (err)
			goto out;
		err = pagetable_update(pagetable, virtual, pagetable_get_physical(pagetable, virtual), pt_flags);
		if (err)
			goto out;
		tlb_flush_range(virtual, page_size);

		pt_flags &= ~PT_HUGEPAGE;
		virtual = (u8*)virtual + page_size;
	}
out:
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return err;
}

int vunmap(void* virtual, size_t size, int flags) {
	if ((uintptr_t)virtual % PAGE_SIZE || size == 0 || !vunmap_validate_flags(flags))
		return -EINVAL;

	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	int err;

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	void* const end = (u8*)virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(&kernel_mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto err;
		}

		const size_t page_size = vma->flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
		physaddr_t physical = 0;
		if (vma->flags & VMM_ALLOC)
			physical = pagetable_get_physical(pagetable, virtual);

		vma_unmap(&kernel_mm_struct, virtual, page_size);
		err = pagetable_unmap(pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "mm: Failed to unmap kernel page, err: %i", err);
			goto err;
		}
		tlb_flush_range(virtual, page_size);

		if (physical && err == 0)
			free_pages(physical, get_order(page_size));

		virtual = (u8*)virtual + page_size;
	}
err:
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return err;
}

void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags) {
	if (!(mmu_flags & MMU_WRITETHROUGH))
		mmu_flags |= MMU_CACHE_DISABLE;

	const size_t page_offset = physical % PAGE_SIZE;
	physaddr_t _physical = physical - page_offset;
	const size_t map_size = size + page_offset;

	const size_t total_size = map_size + PAGE_SIZE * 2;

	u8* const base = vmap(NULL, total_size, mmu_flags, VMM_IOMEM, &_physical);
	if (!base)
		return NULL;

	/* Add guard pages, errors should not happen here */
	if (unlikely(vprotect(base, PAGE_SIZE, MMU_NONE, 0) != 0)) {
		assert(vunmap(base, total_size, 0) == 0);
		return NULL;
	}
	if (unlikely(vprotect(base + PAGE_SIZE + map_size, PAGE_SIZE, MMU_NONE, 0) != 0)) {
		assert(vunmap(base, total_size, 0) == 0);
		return NULL;
	}

	u8* iomem = base + PAGE_SIZE;
	u8* const ret = vmap((void*)iomem, map_size, mmu_flags, VMM_IOMEM | VMM_FIXED, &physical);
	if (unlikely(!ret)) {
		vunmap(base, total_size, 0);
		return NULL;
	}

	return ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	const size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	u8* const base = (u8*)virtual - page_offset - PAGE_SIZE;
	const size_t total_size = size + page_offset + PAGE_SIZE * 2;
	return vunmap(base, total_size, 0);
}

void* vmap_kstack(void) {
	const size_t total_size = KSTACK_SIZE + PAGE_SIZE;

	u8* ptr = vmap(NULL, total_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!ptr)
		return NULL;

	/* Map guard page */
	if (unlikely(vprotect(ptr, PAGE_SIZE, MMU_NONE, 0) != 0)) {
		assert(vunmap(ptr, total_size, 0) == 0);
		return NULL;
	}

	return ptr + total_size;
}

int vunmap_kstack(void* stack) {
	const size_t total_size = KSTACK_SIZE + PAGE_SIZE;
	stack = (u8*)stack - total_size;
	return vunmap(stack, total_size, 0);
}

void vmm_init(void) {
	pagetable_init();

	/* No need to check the pointer, the system would triple fault if an invalid page table was in cr3 */
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	pte_t* cr3 = hhdm_virtual(ctl3_read());
	cpu->mm_struct->pagetable = cr3;

	int best = 0;
	int best_len = 0;
	int current = 0;
	int current_len = 0;

	/* Check for the biggest contiguous space, HHDM mappings can be anywhere in the higher half */
	for (int i = 256; i < PTE_COUNT - 1; i++) {
		if (!(cr3[i] & PT_PRESENT)) {
			if (current_len == 0)
				current = i;
			current_len++;
		} else {
			if (current_len > best_len) {
				best_len = current_len;
				best = current;
			}
			current_len = 0;
		}
	}

	if (current_len > best_len) {
		best_len = current_len;
		best = current;
	}

	kernel_mm_struct.mmap_start = pagetable_get_base_address_from_top_index(best);
	kernel_mm_struct.mmap_end = pagetable_get_base_address_from_top_index(best + best_len);
}

void vmm_switch_mm_struct(struct mm* mm) {
	unsigned long irq = local_irq_save();

	atomic_thread_fence(ATOMIC_SEQ_CST);
	ctl3_write(hhdm_physical(mm->pagetable));
	current_cpu()->mm_struct = mm;

	local_irq_restore(irq);
}
