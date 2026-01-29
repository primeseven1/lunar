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
		uintptr_t virtual, physaddr_t physical, unsigned long pt_flags) {
	if (!(err == -EEXIST && vmm_flags & VMM_FIXED))
		return false;

	bug(vmm_flags & VMM_NOREPLACE);
	err = pagetable_update(pagetable, virtual, physical, pt_flags);
	if (err == -EFAULT) {
		if (pt_flags & PT_HUGEPAGE) {
			for (unsigned long i = 0; i < PTE_COUNT; i++) {
				err = pagetable_unmap(pagetable, virtual + (i * PAGE_SIZE));
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

static int __vmap_physical(pte_t* pagetable, uintptr_t virtual, physaddr_t physical,
		unsigned long pt_flags, size_t page_size, unsigned long count, int vmm_flags) {
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
		uintptr_t virtual, unsigned long pt_flags,
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

		memset(hhdm_virtual(page), 0, page_size);
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

static bool vmap_flags_valid(int flags) {
	if ((flags & VMM_IOMEM && flags & VMM_ALLOC))
		return false;
	if (flags & VMM_PHYSICAL && flags & VMM_ALLOC)
		return false;
	if (flags & VMM_NOREPLACE && !(flags & VMM_FIXED))
		return false;
	if (flags & VMM_USER && (flags & VMM_PHYSICAL || flags & VMM_IOMEM))
		return false;
	return true;
}

static int __vmap(uintptr_t hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional, uintptr_t* out) {
	if (size == 0 || !vmap_flags_valid(flags))
		return -EINVAL;
	if (size > 0x800000000000) /* 128 TiB */
		return -ENOMEM;
	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return -EINVAL;

	const size_t page_size = flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	size = ROUND_UP(size, page_size);
	const unsigned long page_count = size / page_size;
	
	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;
	if (flags & VMM_HUGEPAGE_2M)
		pt_flags |= PT_HUGEPAGE;

	struct mm* mm_struct = &kernel_mm_struct;
	if (flags & VMM_USER) {
		mm_struct = optional ? optional : current_cpu()->mm_struct;
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	mutex_lock(&mm_struct->vma_lock);

	/* If a fixed/replace mapping, the previous state must be saved first */
	struct prevpage* prev_pages = NULL;
	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE))
		prev_pages = prevpage_save(mm_struct, (uintptr_t)hint, size);

	/* Now create a VMA */
	uintptr_t virtual;
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
		err = __vmap_physical(mm_struct->pagetable, (uintptr_t)virtual, physical, pt_flags, page_size, page_count, flags);
		if (err)
			goto err;
	} else if (flags & VMM_ALLOC) {
		err = __vmap_alloc(mm_struct->pagetable, (uintptr_t)virtual, pt_flags, page_size, page_count, flags);
		if (err)
			goto err;
	}

	/* Successful, so now just free previous pages if a fixed/replace, and invalidate TLB's */
	if (prev_pages)
		prevpage_success(prev_pages, PREVPAGE_FREE_PREVIOUS);
	tlb_invalidate((uintptr_t)virtual, size);
	mutex_unlock(&mm_struct->vma_lock);

	*out = virtual;
	return 0;
err:
	/* Unmap a VMA if there is one, and invalidate TLB's since prevpage_fail updates mappings */
	if (virtual)
		vma_unmap(mm_struct, (uintptr_t)virtual, size);
	if (prev_pages)
		prevpage_fail(mm_struct, prev_pages);
	tlb_invalidate((uintptr_t)virtual, size);
	mutex_unlock(&mm_struct->vma_lock);

	return err;
}

static inline bool vprotect_flags_valid(int flags) {
	return (flags == 0 || flags == VMM_USER);
}

static int __vprotect(uintptr_t virtual, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if (virtual & (PAGE_SIZE - 1) || size == 0 || !vprotect_flags_valid(flags))
		return -EINVAL;
	unsigned long pt_flags = pagetable_mmu_to_pt(mmu_flags);
	if (pt_flags == ULONG_MAX)
		return -EINVAL;

	struct mm* mm_struct = &kernel_mm_struct;
	if (flags & VMM_USER) {
		mm_struct = optional ? optional : current_cpu()->mm_struct;
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	int err = 0;
	size_t tlb_flush_round = PAGE_SIZE;

	mutex_lock(&mm_struct->vma_lock);

	struct prevpage* prevpages = prevpage_save(mm_struct, virtual, size);
	const uintptr_t start = virtual;
	const uintptr_t end = virtual + size;

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
		virtual += page_size;
	}
out:
	if (err && prevpages)
		prevpage_fail(mm_struct, prevpages);
	else
		prevpage_success(prevpages, 0);
	tlb_invalidate(start, ROUND_UP(size, tlb_flush_round));
	mutex_unlock(&mm_struct->vma_lock);
	return err;
}

static inline bool vunmap_flags_valid(int flags) {
	return (flags == 0 || flags == VMM_USER);
}

static int __vunmap(uintptr_t virtual, size_t size, int flags, void* optional) {
	if (virtual & (PAGE_SIZE - 1) || size == 0 || !vunmap_flags_valid(flags))
		return -EINVAL;

	struct mm* mm_struct = &kernel_mm_struct;
	if (flags & VMM_USER) {
		mm_struct = optional ? optional : current_cpu()->mm_struct;
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	size_t tlb_invalidate_round = PAGE_SIZE;
	int err;

	mutex_lock(&mm_struct->vma_lock);

	struct prevpage* prevpages = prevpage_save(mm_struct, virtual, size);

	const uintptr_t start = virtual;
	const uintptr_t end = virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, (uintptr_t)virtual);
		if (!vma) {
			err = -ENOENT;
			goto err;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->flags & VMM_HUGEPAGE_2M) {
			page_size = HUGEPAGE_2M_SIZE;
			tlb_invalidate_round = HUGEPAGE_2M_SIZE;

			/* Like in vprotect, this can only happen if the very first page is a hugepage */
			if (unlikely(virtual & (page_size - 1))) {
				err = -EINVAL;
				goto err;
			}
		}

		/* Now remove the VMA and mapping in the page table */
		err = vma_unmap(mm_struct, virtual, page_size);
		if (err)
			goto err;
		err = pagetable_unmap(mm_struct->pagetable, virtual);
		if (unlikely(err)) {
			printk(PRINTK_CRIT "mm: Failed to unmap page, err: %i\n", err);
			goto err;
		}

		virtual += page_size;
	}

err:
	tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	if (err && prevpages) {
		prevpage_fail(mm_struct, prevpages); /* Updates mappings, so another flush is needed */
		tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	} else {
		prevpage_success(prevpages, PREVPAGE_FREE_PREVIOUS);
	}

	mutex_unlock(&mm_struct->vma_lock);
	return err;
}

void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	if ((flags & VMM_USER) || (flags & VMM_IOMEM))
		return ERR_PTR(-EINVAL);
	uintptr_t ret;
	int err = __vmap((uintptr_t)hint, size, mmu_flags, flags, optional, &ret);
	return err ? ERR_PTR(err) : (void*)ret;
}

int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags, void* optional) {
	flags &= ~(VMM_ALLOC | VMM_PHYSICAL | VMM_NOREPLACE | VMM_FIXED | VMM_HUGEPAGE_2M);
	return ((flags & VMM_USER) || (flags & VMM_IOMEM)) ? -EINVAL : __vprotect((uintptr_t)virtual, size, mmu_flags, flags, optional);
}

int vunmap(void* virtual, size_t size, int flags, void* optional) {
	flags &= ~(VMM_ALLOC | VMM_PHYSICAL | VMM_NOREPLACE | VMM_FIXED | VMM_HUGEPAGE_2M);
	return ((flags & VMM_USER) || (flags & VMM_IOMEM)) ? -EINVAL : __vunmap((uintptr_t)virtual, size, flags, optional);
}

void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags) {
	if (!(mmu_flags & MMU_WRITETHROUGH))
		mmu_flags |= MMU_CACHE_DISABLE;

	const size_t page_offset = physical % PAGE_SIZE;
	const size_t total_size = size + page_offset;

	uintptr_t ret;
	int err = __vmap(0, total_size, mmu_flags, VMM_IOMEM, &physical, &ret);
	if (err)
		return NULL;

	return (u8 __iomem*)ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	const size_t total_size = size + page_offset;
	return __vunmap(ROUND_DOWN((uintptr_t)virtual, PAGE_SIZE), total_size, 0, NULL);
}

void __user* usermap(void __user* hint, size_t size, mmuflags_t mmu_flags, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return (__force void __user*)ERR_PTR(-EINVAL);

	mmu_flags |= MMU_USER;
	flags |= VMM_USER;

	uintptr_t ret;
	int err = __vmap((uintptr_t)hint, size, mmu_flags, flags, usermap_info, &ret);
	return err ? (__force void __user*)ERR_PTR(err) : (__force void __user*)ret;
}

int userprotect(void __user* virtual, size_t size, mmuflags_t mmu_flags, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return -EINVAL;

	flags &= ~(VMM_ALLOC | VMM_NOREPLACE | VMM_FIXED | VMM_HUGEPAGE_2M);
	flags |= VMM_USER;
	mmu_flags |= MMU_USER;

	return __vprotect((uintptr_t)virtual, size, mmu_flags, flags, usermap_info);
}

int userunmap(void __user* virtual, size_t size, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return -EINVAL;

	flags &= ~(VMM_ALLOC | VMM_NOREPLACE | VMM_FIXED | VMM_HUGEPAGE_2M);
	flags |= VMM_USER;

	return __vunmap((uintptr_t)virtual, size, flags, usermap_info);
}

static void* __vmap_stack(size_t size, int flags, bool return_top, void* optional) {
	const size_t total_size = size + PAGE_SIZE;
	u8* ptr = vmap(NULL, total_size, MMU_READ | MMU_WRITE, VMM_ALLOC | flags, optional);
	if (IS_PTR_ERR(ptr))
		return ptr;
	int err = vprotect(ptr, PAGE_SIZE, MMU_NONE, 0, NULL);
	if (unlikely(err)) {
		bug(vunmap(ptr, total_size, 0, NULL) != 0);
		return ERR_PTR(err);
	}
	return return_top ? ptr + total_size : ptr;
}

static int __vunmap_stack(void* stack, size_t size, int flags, bool is_top, void* optional) {
	const size_t total_size = size + PAGE_SIZE;
	if (is_top)
		stack = (u8*)stack - total_size;
	return vunmap(stack, total_size, flags, optional);
}

void* vmap_stack(size_t size, bool return_top) {
	return __vmap_stack(size, VMM_ALLOC, return_top, NULL);
}

int vunmap_stack(void* stack, size_t size, bool is_top) {
	return __vunmap_stack(stack, size, 0, is_top, NULL);
}

void __user* uvmap_stack(size_t size, bool return_top, struct mm* mm) {
	return (__force void __user*)__vmap_stack(size, VMM_ALLOC | VMM_USER, return_top, mm);
}

int uvunmap_stack(void __user* stack, size_t size, bool is_top, struct mm* mm) {
	return __vunmap_stack((__force void*)stack, size, VMM_USER, is_top, mm);
}

void vmm_cpu_init(void) {
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	ctl3_write(hhdm_physical(kernel_mm_struct.pagetable));
}

void vmm_init(void) {
	pagetable_init();
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	mm_cache_init();
	pagetable_kmm_init(&kernel_mm_struct);
}
