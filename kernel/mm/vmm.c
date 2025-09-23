#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/asm/ctl.h>
#include <lunar/core/cpu.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/limine.h>
#include <lunar/core/panic.h>
#include <lunar/core/trace.h>
#include <lunar/core/printk.h>
#include <lunar/mm/hhdm.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/vmm.h>
#include <lunar/mm/vma.h>
#include <lunar/lib/string.h>
#include "internal.h"

static struct mm kernel_mm_struct;

static bool handle_pagetable_error(int err, int vmm_flags, pte_t* pagetable, 
		void* virtual, physaddr_t physical, unsigned long pt_flags) {
	if (!(err == -EEXIST && vmm_flags & VMM_FIXED))
		return false;

	bug(vmm_flags & VMM_NOREPLACE);
	err = pagetable_update(pagetable, virtual, physical, pt_flags);
	if (err == -EFAULT) {
		if (pt_flags & PT_HUGEPAGE) {
			for (unsigned long i = 0; i < PTE_COUNT; i++) {
				err = pagetable_unmap(pagetable, (u8*)virtual + (i * PAGE_SIZE));
				bug(err != 0 && err != -ENOENT);
			}
			err = pagetable_map(pagetable, virtual, physical, pt_flags);
		} else {
			bug(pagetable_unmap(pagetable, virtual) != 0);
			err = pagetable_map(pagetable, virtual, physical, pt_flags);
		}
	}

	return !err;
}

static int __vmap_physical(pte_t* pagetable, 
		u8* virtual, physaddr_t physical, unsigned long pt_flags, 
		size_t page_size, unsigned long count, int vmm_flags) {
	int err = 0;
	unsigned long mapped = 0;
	while (count--) {
		err = pagetable_map(pagetable, virtual, physical, pt_flags);
		if (err) {
			if (!handle_pagetable_error(err, vmm_flags, pagetable, virtual, physical, pt_flags)) {
				while (mapped--) {
					virtual -= page_size;
					bug(pagetable_unmap(pagetable, virtual) != 0);
				}
				break;
			}
			err = 0;
		}
		mapped++;
		virtual += page_size;
		physical += page_size;
	}

	return err;
}

static int __vmap_alloc(pte_t* pagetable, 
		u8* virtual, unsigned long pt_flags,
		size_t page_size, unsigned long count, 
		int vmm_flags, mm_t mm_flags) {
	int err;
	unsigned int order = get_order(page_size);
	unsigned long mapped = 0;
	while (count--) {
		physaddr_t page = alloc_pages(mm_flags, order);
		if (!page) {
			err = -ENOMEM;
			goto cleanup;
		}
		err = pagetable_map(pagetable, virtual, page, pt_flags);
		if (err && !handle_pagetable_error(err, vmm_flags, pagetable, virtual, page, pt_flags))
			goto cleanup;
		mapped++;
		virtual += page_size;
	}

	return 0;
cleanup:
	while (mapped--) {
		virtual -= page_size;
		physaddr_t page = pagetable_get_physical(pagetable, virtual);
		bug(page == 0);
		bug(pagetable_unmap(pagetable, virtual) != 0);
		free_pages(page, order);
	}
	return err;
}

void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if ((flags & VMM_IOMEM && flags & VMM_ALLOC) ||
			(flags & VMM_PHYSICAL && flags & VMM_ALLOC) ||
			(flags & VMM_NOREPLACE && !(flags & VMM_FIXED)))
		return NULL;
	if (size == 0)
		return NULL;

	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return NULL;

	const size_t page_size = flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	const int page_shift = flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SHIFT : PAGE_SHIFT;
	if (size > SIZE_MAX - (page_size - 1))
		return NULL;
	size = ROUND_UP(size, page_size);
	const unsigned long page_count = size >> page_shift;
	
	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;
	if (flags & VMM_HUGEPAGE_2M)
		flags |= PT_HUGEPAGE;

	pte_t* pagetable = kernel_mm_struct.pagetable;
	struct prevpage* prev_pages = NULL;

	mutex_lock(&kernel_mm_struct.vma_list_lock);

	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE))
		prev_pages = prevpage_save(&kernel_mm_struct, hint, size);

	void* virtual = NULL;
	int err = vma_map(&kernel_mm_struct, hint, size, mmu_flags, flags, &virtual);
	if (err)
		goto cleanup;

	if (flags & VMM_PHYSICAL) {
		if (!optional)
			goto cleanup;
		physaddr_t physical = *(physaddr_t*)optional;
		if (physical & (page_size - 1))
			goto cleanup;
		err = __vmap_physical(pagetable, virtual, physical, pt_flags, page_size, page_count, flags);
		if (err)
			goto cleanup;
	} else if (flags & VMM_ALLOC) {
		mm_t mm = optional ? *(mm_t*)optional : MM_ZONE_NORMAL;
		/* Map a present and writable page, so that way the memory can be zeroed before */
		err = __vmap_alloc(pagetable, virtual, pt_flags | PT_READ_WRITE | PT_PRESENT,
				page_size, page_count, flags, mm);
		if (err)
			goto cleanup;
	}

	tlb_invalidate(virtual, size);
	if (flags & VMM_ALLOC)
		memset(virtual, 0, size);
	if (prev_pages)
		prevpage_success(prev_pages, PREVPAGE_FREE_PREVIOUS);
	mutex_unlock(&kernel_mm_struct.vma_list_lock);

	/* Make sure the memory is now mapped correctly */
	if (flags & VMM_ALLOC && (!(mmu_flags & MMU_WRITE) || !(mmu_flags & MMU_READ)))
		bug(vprotect(virtual, size, mmu_flags, 0) != 0);
	return virtual;
cleanup:
	if (virtual)
		bug(vma_unmap(&kernel_mm_struct, virtual, size) != 0);
	if (prev_pages) {
		prevpage_fail(&kernel_mm_struct, prev_pages);
		tlb_invalidate(virtual, size); /* Invalidate just in case */
	}
	mutex_unlock(&kernel_mm_struct.vma_list_lock);
	return NULL;
}

int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags) {
	if ((uintptr_t)virtual & (PAGE_SIZE - 1) || size == 0 || flags != 0)
		return -EINVAL;
	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return -EINVAL;

	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	int err = 0;
	size_t tlb_flush_round = PAGE_SIZE;

	mutex_lock(&kernel_mm_struct.vma_list_lock);

	struct prevpage* prevpages = prevpage_save(&kernel_mm_struct, virtual, size);

	void* const start = virtual;
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
			tlb_flush_round = HUGEPAGE_2M_SIZE;

			/* This can only happen if the very first page being remapped is a hugepage */
			if (unlikely((uintptr_t)virtual & (page_size - 1))) {
				err = -EINVAL;
				goto out;
			}
		}


		err = vma_protect(&kernel_mm_struct, virtual, page_size, mmu_flags);
		if (err)
			goto out;
		err = pagetable_update(pagetable, virtual, pagetable_get_physical(pagetable, virtual), pt_flags);
		if (err)
			goto out;

		pt_flags &= ~PT_HUGEPAGE;
		virtual = (u8*)virtual + page_size;
	}
out:
	if (err && prevpages)
		prevpage_fail(&kernel_mm_struct, prevpages);
	else
		prevpage_success(prevpages, 0);
	tlb_invalidate(start, ROUND_UP(size, tlb_flush_round));
	mutex_unlock(&kernel_mm_struct.vma_list_lock);
	return err;
}

int vunmap(void* virtual, size_t size, int flags) {
	if ((uintptr_t)virtual & (PAGE_SIZE - 1) || size == 0 || flags != 0)
		return -EINVAL;

	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	size_t tlb_invalidate_round = PAGE_SIZE;
	int err;

	mutex_lock(&kernel_mm_struct.vma_list_lock);

	struct prevpage* prevpages = prevpage_save(&kernel_mm_struct, virtual, size);

	void* const start = virtual;
	void* const end = (u8*)virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(&kernel_mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto err;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->flags & VMM_HUGEPAGE_2M) {
			page_size = HUGEPAGE_2M_SIZE;
			tlb_invalidate_round = HUGEPAGE_2M_SIZE;
			if (unlikely((uintptr_t)virtual & (page_size - 1))) {
				err = -EINVAL;
				goto err;
			}
		}

		vma_unmap(&kernel_mm_struct, virtual, page_size);
		err = pagetable_unmap(pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "mm: Failed to unmap kernel page, err: %i", err);
			goto err;
		}

		virtual = (u8*)virtual + page_size;
	}

err:
	tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	if (err && prevpages) {
		prevpage_fail(&kernel_mm_struct, prevpages);
		tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	} else {
		prevpage_success(prevpages, PREVPAGE_FREE_PREVIOUS);
	}

	mutex_unlock(&kernel_mm_struct.vma_list_lock);
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
		bug(vunmap(base, total_size, 0) != 0);
		return NULL;
	}
	if (unlikely(vprotect(base + PAGE_SIZE + map_size, PAGE_SIZE, MMU_NONE, 0) != 0)) {
		bug(vunmap(base, total_size, 0) != 0);
		return NULL;
	}

	u8* iomem = base + PAGE_SIZE;
	u8* const ret = vmap(iomem, map_size, mmu_flags, VMM_IOMEM | VMM_FIXED, &physical);
	if (unlikely(!ret)) {
		vunmap(base, total_size, 0);
		return NULL;
	}

	return (u8 __iomem*)ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	const size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	u8 __iomem* const base = (u8 __iomem*)virtual - page_offset - PAGE_SIZE;
	const size_t total_size = size + page_offset + PAGE_SIZE * 2;
	return vunmap((void __force*)base, total_size, 0);
}

void* vmap_kstack(void) {
	const size_t total_size = KSTACK_SIZE + PAGE_SIZE;

	u8* ptr = vmap(NULL, total_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!ptr)
		return NULL;

	/* Map guard page */
	if (unlikely(vprotect(ptr, PAGE_SIZE, MMU_NONE, 0) != 0)) {
		bug(vunmap(ptr, total_size, 0) != 0);
		return NULL;
	}

	return ptr + total_size;
}

int vunmap_kstack(void* stack) {
	const size_t total_size = KSTACK_SIZE + PAGE_SIZE;
	stack = (u8*)stack - total_size;
	return vunmap(stack, total_size, 0);
}

void vmm_cpu_init(void) {
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	ctl3_write(hhdm_physical(kernel_mm_struct.pagetable));
}

void vmm_init(void) {
	pagetable_init();

	/* No need to check the pointer, the system would triple fault if an invalid page table was in cr3 */
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	pte_t* cr3 = hhdm_virtual(ctl3_read());
	cpu->mm_struct->pagetable = cr3;
	mutex_init(&cpu->mm_struct->vma_list_lock);
	list_head_init(&cpu->mm_struct->vma_list);

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
