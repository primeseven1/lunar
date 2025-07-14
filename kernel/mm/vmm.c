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

#define GUARD_4KPAGE_COUNT 1

static unsigned long mmu_to_pt(mmuflags_t mmu_flags) {
	if (mmu_flags & MMU_CACHE_DISABLE && mmu_flags & MMU_WRITETHROUGH)
		return 0;

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
	if (flags & VMM_IOMEM && flags & VMM_ALLOC)
		return false;
	if (flags & VMM_PHYSICAL && flags & VMM_ALLOC)
		return false;

	return true;
}

static inline bool vprotect_validate_flags(int flags) {
	return flags == 0;
}

static inline bool vunmap_validate_flags(int flags) {
	if (flags & VMM_ALLOC || flags & VMM_PHYSICAL)
		return false;

	return true;
}

#define GUARD_SIZE (PAGE_SIZE * GUARD_4KPAGE_COUNT)

static struct mm kernel_mm_struct;

static bool check_pagetable_error(pte_t* pagetable, void* virtual, 
		physaddr_t physical, unsigned long pt_flags, int err, int flags) {
	if (err == 0)
		return true;

	/* Simply update the mapping if we want a fixed mapping */
	if (err == -EEXIST && flags & VMM_FIXED) {
		assert(!(flags & VMM_NOREPLACE));
		err = pagetable_update(pagetable, virtual, physical, pt_flags);
		if (err)
			return false;
		return true;
	}

	return false;
}

void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if (!vmap_validate_flags(flags))
		return NULL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == 0)
		return NULL;

	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (page_count == 0)
		return NULL;

	const size_t page_size = PAGE_SIZE;
	const size_t vma_size = size + GUARD_SIZE;
	const unsigned int page_size_order = get_order(page_size);

	u8* virtual;
	int err = vma_map(&kernel_mm_struct, hint, vma_size, mmu_flags, flags, (void**)&virtual);
	if (err)
		return NULL;

	u8* ret = virtual;
	pte_t* pagetable = current_cpu()->mm_struct->pagetable;

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	unsigned long mapped = 0;
	if (flags & VMM_PHYSICAL) {
		if (!optional)
			goto cleanup;
		physaddr_t physical = *(physaddr_t*)optional;
		while (page_count--) {
			err = pagetable_map(pagetable, virtual, physical, pt_flags);
			if (!check_pagetable_error(pagetable, virtual, physical, pt_flags, err, flags))
				goto cleanup;

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
				goto cleanup;
			err = pagetable_map(pagetable, virtual, physical, pt_flags);
			if (!check_pagetable_error(pagetable, virtual, physical, pt_flags, err, flags))
				goto cleanup;

			tlb_flush_range(virtual, page_size);
			mapped++;
			virtual += page_size;
		}
	} else {
		goto cleanup;
	}

	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return ret;
cleanup:
	while (mapped--) {
		virtual -= page_size;
		if (flags & VMM_ALLOC) {
			physaddr_t physical = pagetable_get_physical(pagetable, virtual);
			free_pages(physical, page_size_order);
		}
		pagetable_unmap(pagetable, virtual);
		tlb_flush_range(virtual, page_size);
	}

	err = vma_unmap(&kernel_mm_struct, virtual, vma_size);
	if (err)
		printk(PRINTK_ERR "mm: vma_unmap failed in error handling of %s (err: %i)\n", __func__, err);
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return NULL;
}

static bool full_overlap_check(struct mm* mm, const void* virtual, size_t size) {
	const u8* v8 = virtual;
	size_t size_remaining = size;

	while (1) {
		struct vma* vma = vma_find(mm, v8);
		if (!vma)
			return false;

		size_t covered = vma->top - (uintptr_t)v8;
		if (covered >= size_remaining)
			return true;

		size_remaining -= covered;
		v8 += covered;
	}
}

int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags) {
	if ((uintptr_t)virtual % PAGE_SIZE || !vprotect_validate_flags(flags))
		return -EINVAL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == 0)
		return -EINVAL;
	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (page_count == 0)
		return -EINVAL;

	size_t page_size = PAGE_SIZE;
	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	int err = 0;

	unsigned int old_mmu_size_order = get_order(page_count * sizeof(mmuflags_t));
	physaddr_t _old_mmu = alloc_pages(MM_ZONE_NORMAL, old_mmu_size_order);
	if (!_old_mmu)
		return -ENOMEM;
	mmuflags_t* old_mmu = hhdm_virtual(_old_mmu);
	memset(old_mmu, 0, page_count * sizeof(mmuflags_t));

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	if (!full_overlap_check(&kernel_mm_struct, virtual, size)) {
		err = -ENOENT;
		goto out;
	}

	void* tmp = virtual;

	for (unsigned long i = 0; i < page_count; i++) {
		struct vma* vma = vma_find(&kernel_mm_struct, virtual);
		old_mmu[i] = vma->prot;
		err = vma_protect(&kernel_mm_struct, virtual, page_size, mmu_flags);
		if (err)
			goto undo;
		err = pagetable_update(pagetable, virtual, pagetable_get_physical(pagetable, virtual), pt_flags);
		if (err)
			goto undo;
		virtual = (u8*)virtual + page_size;
	}


	tlb_flush_range(virtual, size);
	goto out;
undo:
	virtual = tmp;
	for (unsigned long i = 0; i < page_count - 1; i++) {
		if (old_mmu[i] == 0)
			break;

		pt_flags = mmu_to_pt(old_mmu[i]);
		physaddr_t physical = pagetable_get_physical(pagetable, virtual);
		assert(vma_protect(&kernel_mm_struct, virtual, page_size, old_mmu[i]) == 0);
		assert(pagetable_update(pagetable, virtual, physical, pt_flags) == 0);

		virtual = (u8*)virtual + page_size;
	}

	/* There shouldn't be a need for a TLB flush here, but do it again anyway to be safe */
	tlb_flush_range(virtual, size);

out:
	free_pages(_old_mmu, old_mmu_size_order);
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return err;
}

int vunmap(void* virtual, size_t size, int flags) {
	if ((uintptr_t)virtual % PAGE_SIZE || !vunmap_validate_flags(flags))
		return -EINVAL;
	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (page_count == 0)
		return -EINVAL;

	pte_t* pagetable = current_cpu()->mm_struct->pagetable;
	int err;

	unsigned long irq;
	spinlock_lock_irq_save(&kernel_mm_struct.vma_list_lock, &irq);

	while (page_count) {
		struct vma* vma = vma_find(&kernel_mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto err;
		}

		const size_t page_size = PAGE_SIZE;
		vma_unmap(&kernel_mm_struct, virtual, page_size);
		if (vma->flags & VMM_ALLOC) {
			physaddr_t mem = pagetable_get_physical(pagetable, virtual);
			free_pages(mem, get_order(page_size));
		}
		err = pagetable_unmap(pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "mm: Failed to unmap kernel page, err: %i", err);
			goto err;
		}
		tlb_flush_range(virtual, page_size);

		virtual = (u8*)virtual + page_size;
		page_count--;
	}

#if GUARD_SIZE > 0
	vma_unmap(&kernel_mm_struct, virtual, GUARD_SIZE);
#endif /* GUARD_SIZE > 0 */
err:
	spinlock_unlock_irq_restore(&kernel_mm_struct.vma_list_lock, &irq);
	return err;
}

void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags) {
	if (!(mmu_flags & MMU_WRITETHROUGH))
		mmu_flags |= MMU_CACHE_DISABLE;

	size_t page_offset = physical % PAGE_SIZE;
	physaddr_t _physical = physical - page_offset;
	u8 __iomem* ret = vmap(NULL, size + page_offset, mmu_flags, VMM_IOMEM, &_physical);
	if (!ret)
		return NULL;
	return ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	void* _virtual = (u8*)virtual - page_offset;
	return vunmap(_virtual, size + page_offset, 0);
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
