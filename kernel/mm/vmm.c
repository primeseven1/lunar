#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/asm/ctl.h>
#include <lunar/core/cpu.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/limine.h>
#include <lunar/core/panic.h>
#include <lunar/core/trace.h>
#include <lunar/core/printk.h>
#include <lunar/sched/scheduler.h>
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
		int vmm_flags) {
	int err;
	unsigned int order = get_order(page_size);
	unsigned long mapped = 0;
	while (count--) {
		physaddr_t page = alloc_pages(MM_ZONE_NORMAL, order);
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

static inline bool vmap_flags_valid(int flags) {
	return !((flags & VMM_IOMEM && flags & VMM_ALLOC) ||
			(flags & VMM_PHYSICAL && flags & VMM_ALLOC) ||
			(flags & VMM_NOREPLACE && !(flags & VMM_FIXED)));
}

void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if (size == 0 || !vmap_flags_valid(flags))
		return ERR_PTR(-EINVAL);
	if (size > 0x800000000000) /* 128 TiB */
		return ERR_PTR(-ENOMEM);
	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return ERR_PTR(-EINVAL);

	const size_t page_size = flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	size = ROUND_UP(size, page_size);
	const unsigned long page_count = size / page_size;
	
	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;
	if (flags & VMM_HUGEPAGE_2M)
		flags |= PT_HUGEPAGE;

	struct mm* mm_struct;
	if (flags & VMM_USER) {
		mm_struct = current_cpu()->mm_struct;
		/* kernel thread, cannot map a user pointer */
		if (mm_struct == &kernel_mm_struct)
			return ERR_PTR(-EINVAL);
	} else {
		mm_struct = &kernel_mm_struct;
	}

	mutex_lock(&mm_struct->vma_list_lock);

	/* If a fixed/replace mapping, the previous state must be saved first */
	struct prevpage* prev_pages = NULL;
	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE))
		prev_pages = prevpage_save(mm_struct, hint, size);

	/* Now create a VMA */
	void* virtual = NULL;
	int err = vma_map(mm_struct, hint, size, mmu_flags, flags, &virtual);
	if (err)
		goto err;

	/* Actually map the memory in the page table */
	if (flags & VMM_PHYSICAL) {
		err = -EINVAL;
		if (!optional)
			goto err;
		physaddr_t physical = *(physaddr_t*)optional;
		if (physical & (page_size - 1))
			goto err;
		err = __vmap_physical(mm_struct->pagetable, virtual, physical, pt_flags, page_size, page_count, flags);
		if (err)
			goto err;
	} else if (flags & VMM_ALLOC) {
		err = __vmap_alloc(mm_struct->pagetable, virtual, pt_flags, page_size, page_count, flags);
		if (err)
			goto err;
	}

	/* Successful, so now just free previous pages if a fixed/replace, and invalidate TLB's */
	if (prev_pages)
		prevpage_success(prev_pages, PREVPAGE_FREE_PREVIOUS);
	tlb_invalidate(virtual, size);
	mutex_unlock(&mm_struct->vma_list_lock);

	return virtual;
err:
	/* Unmap a VMA if there is one, and invalidate TLB's since prevpage_fail updates mappings */
	if (virtual)
		vma_unmap(mm_struct, virtual, size);
	if (prev_pages)
		prevpage_fail(mm_struct, prev_pages);
	tlb_invalidate(virtual, size);
	mutex_unlock(&mm_struct->vma_list_lock);

	return ERR_PTR(err);
}

static inline bool vprotect_flags_valid(int flags) {
	return flags == 0;
}

int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags) {
	if ((uintptr_t)virtual & (PAGE_SIZE - 1) || size == 0 || !vprotect_flags_valid(flags))
		return -EINVAL;
	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return -EINVAL;

	struct mm* mm_struct = current_cpu()->mm_struct;
	int err = 0;
	size_t tlb_flush_round = PAGE_SIZE;

	mutex_lock(&mm_struct->vma_list_lock);

	struct prevpage* prevpages = prevpage_save(mm_struct, virtual, size);

	void* const start = virtual;
	void* const end = (u8*)virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
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

		/* Now just update the VMA and pagetable */
		err = vma_protect(mm_struct, virtual, page_size, mmu_flags);
		if (err)
			goto out;
		err = pagetable_update(mm_struct->pagetable, virtual, pagetable_get_physical(mm_struct->pagetable, virtual), pt_flags);
		if (unlikely(err))
			goto out;

		pt_flags &= ~PT_HUGEPAGE;
		virtual = (u8*)virtual + page_size;
	}
out:
	if (err && prevpages)
		prevpage_fail(mm_struct, prevpages);
	else
		prevpage_success(prevpages, 0);
	tlb_invalidate(start, ROUND_UP(size, tlb_flush_round));
	mutex_unlock(&mm_struct->vma_list_lock);
	return err;
}

static inline bool vunmap_flags_valid(int flags) {
	return flags == 0;
}

int vunmap(void* virtual, size_t size, int flags) {
	if ((uintptr_t)virtual & (PAGE_SIZE - 1) || size == 0 || !vunmap_flags_valid(flags))
		return -EINVAL;

	struct mm* mm_struct = current_cpu()->mm_struct;
	size_t tlb_invalidate_round = PAGE_SIZE;
	int err;

	mutex_lock(&mm_struct->vma_list_lock);

	struct prevpage* prevpages = prevpage_save(mm_struct, virtual, size);

	void* const start = virtual;
	void* const end = (u8*)virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto err;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->flags & VMM_HUGEPAGE_2M) {
			page_size = HUGEPAGE_2M_SIZE;
			tlb_invalidate_round = HUGEPAGE_2M_SIZE;

			/* Like in vprotect, this can only happen if the very first page is a hugepage */
			if (unlikely((uintptr_t)virtual & (page_size - 1))) {
				err = -EINVAL;
				goto err;
			}
		}

		/* Now remove the VMA and mapping in the page table */
		vma_unmap(mm_struct, virtual, page_size);
		err = pagetable_unmap(mm_struct->pagetable, virtual);
		if (unlikely(err)) {
			printk(PRINTK_CRIT "mm: Failed to unmap page, err: %i\n", err);
			goto err;
		}

		virtual = (u8*)virtual + page_size;
	}

err:
	tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	if (err && prevpages) {
		prevpage_fail(mm_struct, prevpages); /* Updates mappings, so another flush is needed */
		tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	} else {
		prevpage_success(prevpages, PREVPAGE_FREE_PREVIOUS);
	}

	mutex_unlock(&mm_struct->vma_list_lock);
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
	if (IS_PTR_ERR(base))
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
	if (unlikely(IS_PTR_ERR(ret))) {
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
	if (IS_PTR_ERR(ptr))
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
	irqflags_t irq = local_irq_save();

	atomic_thread_fence(ATOMIC_SEQ_CST);
	ctl3_write(hhdm_physical(mm->pagetable));
	current_cpu()->mm_struct = mm;

	local_irq_restore(irq);
}
