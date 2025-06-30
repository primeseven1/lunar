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

static struct vma* kvma;
static struct vma* iovma;

/* Lock for all kernel space page tables, since all kernel mappings are shared */
static spinlock_t kernel_pt_lock = SPINLOCK_INITIALIZER;

const struct vma* hhdmvma;

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

static inline bool in_vma(struct vma* vma, const void* virtual, size_t size) {
	uintptr_t virtual_top;
	if (__builtin_add_overflow((uintptr_t)virtual, size, &virtual_top))
		return false;
	else if (virtual < vma->start || virtual_top > (uintptr_t)vma->end)
		return false;
	
	return true;
}

static inline bool vmap_validate_flags(unsigned int flags) {
	if (flags & VMAP_FREE)
		return false;
	if (flags & VMAP_IOMEM && flags & VMAP_ALLOC)
		return false;
	if (flags & VMAP_PHYSICAL && flags & VMAP_ALLOC)
		return false;

	return true;
}

static inline bool vprotect_validate_flags(unsigned int flags) {
	flags &= ~VMAP_HUGEPAGE;
	return flags == 0;
}

static inline bool vunmap_validate_flags(unsigned int flags) {
	if (flags & VMAP_ALLOC || flags & VMAP_PHYSICAL)
		return false;

	return true;
}

void* vmap(void* hint, size_t size, unsigned int flags, mmuflags_t mmu_flags, void* optional) {
	if (hint)
		printk(PRINTK_WARN "mm: vmap hint ignored!\n");

	/* Check for any conflicting flags */
	if (mmu_flags & MMU_USER || !vmap_validate_flags(flags))
		return NULL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (!pt_flags)
		return NULL;

	/* Get the page count and then save the number of pages to allocate in the VMA in case of failure */
	unsigned long page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (page_count == 0)
		return NULL;

	unsigned long alloc_page_count = page_count + GUARD_4KPAGE_COUNT;
	size_t page_size = PAGE_SIZE;
	if (flags & VMAP_HUGEPAGE) {
		page_count = ROUND_UP(page_count, PTE_COUNT);
		page_size = HUGEPAGE_SIZE;
		pt_flags |= PT_HUGEPAGE;
		alloc_page_count = page_count + GUARD_4KPAGE_COUNT;
		page_count /= PTE_COUNT;
	}
	const unsigned int page_size_order = get_order(page_size);

	struct vma* vma = kvma;
	if (flags & VMAP_IOMEM) {
		flags |= VMAP_PHYSICAL;
		vma = iovma;
	}

	u8* virtual = vma_alloc_pages(vma, alloc_page_count, page_size);
	if (!virtual)
		return NULL;
	u8* ret = virtual;
	pte_t* pagetable = current_cpu()->vmm_ctx.pagetable;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);

	unsigned long pages_mapped = 0;
	if (flags & VMAP_PHYSICAL) {
		if (!optional) {
			ret = NULL;
			goto cleanup;
		}
		physaddr_t physical = *(physaddr_t*)optional;
		while (page_count--) {
			if (pagetable_map(pagetable, virtual, physical, pt_flags)) {
				ret = NULL;
				goto cleanup;
			}

			tlb_flush_single(virtual);
			pages_mapped++;
			virtual += page_size;
			physical += page_size;
		}
	} else if (flags & VMAP_ALLOC) {
		mm_t mm_flags = MM_ZONE_NORMAL;
		if (optional)
			mm_flags = *(mm_t*)optional;

		while (page_count--) {
			physaddr_t physical = alloc_pages(mm_flags, page_size_order);
			if (!physical) {
				ret = NULL;
				goto cleanup;
			}
			if (pagetable_map(pagetable, virtual, physical, pt_flags)) {
				ret = NULL;
				goto cleanup;
			}
		
			tlb_flush_single(virtual);
			pages_mapped++;
			virtual += page_size;
		}
	} else {
		ret = NULL;
	}

cleanup:
	if (!ret) {
		while (pages_mapped--) {
			virtual -= page_size;
			if (flags & VMAP_ALLOC) {
				physaddr_t physical = pagetable_get_physical(pagetable, virtual);
				free_pages(physical, page_size_order);
			}
			pagetable_unmap(pagetable, virtual);
			tlb_flush_single(virtual);
		}
		vma_free_pages(vma, virtual, alloc_page_count);
	}

	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	return ret;
}

int vprotect(void* virtual, size_t size, unsigned int flags, mmuflags_t mmu_flags) {
	if (!vprotect_validate_flags(flags))
		return -EINVAL;
	unsigned long page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (page_count == 0)
		return -EINVAL;
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (!pt_flags)
		return -EINVAL;

	size_t page_size = PAGE_SIZE;
	if (flags & VMAP_HUGEPAGE) {
		page_size = HUGEPAGE_SIZE;
		page_count = (page_count + PTE_COUNT - 1) / PTE_COUNT;
		pt_flags |= PT_HUGEPAGE;
	}

	pte_t* pagetable = current_cpu()->vmm_ctx.pagetable;
	int err;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);

	unsigned long pages_remapped = 0;
	while (page_count--) {
		physaddr_t mem = pagetable_get_physical(pagetable, virtual);
		err = pagetable_update(pagetable, virtual, mem, pt_flags);
		if (err) {
			if (pages_remapped != 0) {
				printk(PRINTK_CRIT "mm: Failed to remap all pages, PTE's are unreliable!\n");
				dump_stack();
			}

			goto cleanup;
		}

		virtual = (u8*)virtual + page_size;
	}

cleanup:
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	return err;
}

int vunmap(void* virtual, size_t size, unsigned int flags) {
	unsigned long page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (page_count == 0 || !vunmap_validate_flags(flags))
		return -EINVAL;

	struct vma* vma;
	if (in_vma(kvma, virtual, size))
		vma = kvma;
	else if (in_vma(iovma, virtual, size))
		vma = iovma;
	else
		return -EFAULT;

	unsigned long alloc_page_count = page_count + GUARD_4KPAGE_COUNT;
	size_t page_size = PAGE_SIZE;
	if (flags & VMAP_HUGEPAGE) {
		page_size = HUGEPAGE_SIZE;
		page_count = ROUND_UP(page_count, PTE_COUNT);
		alloc_page_count = page_count + GUARD_4KPAGE_COUNT;
		page_count /= PTE_COUNT;
	}
	const unsigned int page_size_order = get_order(page_size);

	pte_t* pagetable = current_cpu()->vmm_ctx.pagetable;
	int err;

	void* saved_virtual = virtual;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	while (page_count--) {
		if (flags & VMAP_FREE) {
			physaddr_t mem = pagetable_get_physical(pagetable, virtual);
			free_pages(mem, page_size_order);
		}
		err = pagetable_unmap(pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "Failed to unmap kernel page, err: %i", err);
			goto err;
		}

		tlb_flush_single(virtual);
		virtual = (u8*)virtual + page_size;
	}

	vma_free_pages(vma, saved_virtual, alloc_page_count);
err:
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	return err;
}

void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags) {
	if (!(mmu_flags & MMU_WRITETHROUGH))
		mmu_flags |= MMU_CACHE_DISABLE;

	size_t page_offset = physical % PAGE_SIZE;
	physaddr_t _physical = physical - page_offset;
	u8 __iomem* ret = vmap(NULL, size + page_offset, VMAP_IOMEM, mmu_flags, &_physical);
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
	pte_t* cr3 = hhdm_virtual(ctl3_read());
	current_cpu()->vmm_ctx.pagetable = cr3;

	void* hhdm_start = hhdm_virtual(0);
	unsigned int pte_index = (uintptr_t)hhdm_start >> 39 & 0x01FF;
	hhdm_start = pagetable_get_base_address_from_top_index(pte_index);
	void* hhdm_end = NULL;
	for (unsigned int i = pte_index; i < PTE_COUNT; i++) {
		if (!(cr3[i] & PT_PRESENT)) {
			hhdm_end = pagetable_get_base_address_from_top_index(i);
			break;
		}
	}
	hhdmvma = vma_create(hhdm_start, hhdm_end, 0);
	assert(hhdmvma != NULL);

	for (unsigned int i = 256; i < PTE_COUNT; i++) {
		if (!(cr3[i] & PT_PRESENT)) {
			void* start = pagetable_get_base_address_from_top_index(i);
			void* end = pagetable_get_end_address_from_top_index(i);
			if (!kvma)
				kvma = vma_create(start, end, 0);
			else if (!iovma)
				iovma = vma_create(start, end, 0);
			else
				break;
		}
	}

	assert(kvma != NULL && iovma != NULL);
}
