#include <lunar/common.h>
#include <lunar/printk.h>
#include <lunar/compiler.h>
#include <lunar/vmm.h>
#include <lunar/string.h>
#include <lunar/panic.h>
#include <lunar/percpu.h>
#include <lunar/init.h>
#include <lunar/irq.h>
#include <arch/page.h>
#include "internal.h"

static struct mm kernel_mm_struct;

static bool handle_pagetable_error(int err, int vmm_flags, pte_t* pagetable, 
		uintptr_t virtual, physaddr_t physical, pgprot_t prot) {
	if (!(err == -EEXIST && vmm_flags & VMM_FIXED))
		return false;

	bug(vmm_flags & VMM_NOREPLACE);
	const bool hugetlb = !!(vmm_flags & VMM_HUGETLB);

	err = arch_pagetable_update(pagetable, virtual, physical, hugetlb, prot);
	if (err == -EFAULT) {
		if (hugetlb) {
			virtual = ROUND_DOWN(virtual, PMD_SIZE);
			const uintptr_t end = virtual + PMD_SIZE;
			while (virtual < end) {
				err = arch_pagetable_unmap(pagetable, virtual);
				bug(err != 0 && err != -ENOENT);
				virtual += PAGE_SIZE;
			}
			err = arch_pagetable_map(pagetable, virtual, physical, hugetlb, prot);
		} else {
			bug(arch_pagetable_unmap(pagetable, virtual) != 0);
			err = arch_pagetable_map(pagetable, virtual, physical, hugetlb, prot);
		}
	}

	return !err;
}

static int __vmap_physical(int vmm_flags, pte_t* pagetable,
		uintptr_t virtual, physaddr_t physical, pgprot_t prot, size_t page_size, size_t count) {
	int err = 0;
	size_t mapped = 0;
	const bool hugetlb = !!(vmm_flags & VMM_HUGETLB);

	while (count--) {
		err = arch_pagetable_map(pagetable, virtual, physical, hugetlb, prot);
		if (err) {
			if (!handle_pagetable_error(err, vmm_flags, pagetable, virtual, physical, prot)) {
				while (mapped--) {
					virtual -= page_size;
					bug(arch_pagetable_unmap(pagetable, virtual) != 0);
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

static int __vmap_alloc(int vmm_flags, pte_t* pagetable, uintptr_t virtual, pgprot_t prot, size_t page_size, size_t count) {
	int err;

	unsigned int order = get_order(page_size);
	size_t mapped = 0;
	const bool hugetlb = !!(vmm_flags & VMM_HUGETLB);

	while (count--) {
		physaddr_t page = alloc_pages(MM_ZONE_NORMAL, order);
		if (!page) {
			err = -ENOMEM;
			goto cleanup;
		}
		err = arch_pagetable_map(pagetable, virtual, page, hugetlb, prot);
		if (err && !handle_pagetable_error(err, vmm_flags, pagetable, virtual, page, prot))
			goto cleanup;

		memset(hhdm_virtual(page), 0, page_size);
		mapped++;
		virtual += page_size;
	}

	return 0;
cleanup:
	while (mapped--) {
		virtual -= page_size;
		physaddr_t page = arch_pagetable_get_physical(pagetable, virtual);
		bug(page == 0);
		bug(arch_pagetable_unmap(pagetable, virtual) != 0);
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

static int __vmap(uintptr_t hint, size_t size, pgprot_t prot, int flags, void* optional, uintptr_t* out) {
	if (size == 0 || !vmap_flags_valid(flags))
		return -EINVAL;
	if (size > 0x800000000000) /* 128 TiB */
		return -ENOMEM;

	if (flags & VMM_HUGETLB) {
		if (flags & VMM_HUGETLB_1GB || PMD_SIZE != 0x200000)
			return -ENOTSUP;
		else
			flags |= VMM_HUGETLB_2MB;
	} else {
		flags &= ~(VMM_HUGETLB_2MB | VMM_HUGETLB_1GB);
	}

	const size_t page_size = flags & VMM_HUGETLB_2MB ? PMD_SIZE : PAGE_SIZE;
	size = ROUND_UP(size, page_size);
	const unsigned long page_count = size / page_size;
	
	if (flags & VMM_IOMEM)
		flags |= VMM_PHYSICAL;

	struct mm* mm_struct = &kernel_mm_struct;
	if (flags & VMM_USER) {
		unsigned long irq_flags = local_irq_save();
		mm_struct = optional ? ((struct vmm_usermap_info*)optional)->mm_struct : current_cpu()->mm_struct;
		local_irq_restore(irq_flags);
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	int err;
	mutex_acquire(&mm_struct->mutex);

	/* If a fixed/replace mapping, create a snapshot of the current state to restore */
	struct page_snapshot* prev_pages = NULL;
	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE)) {
		prev_pages = snapshot_pages(mm_struct, (uintptr_t)hint, size);
		if (unlikely(IS_PTR_ERR(prev_pages))) {
			err = PTR_ERR(prev_pages);
			goto err_novma;
		}
	}
	uintptr_t virtual;
	err = vma_map(mm_struct, hint, size, prot, flags, &virtual);
	if (err) {
		snapshot_cleanup(prev_pages, false);
		goto err_novma;
	}

	/* Actually map the memory in the page table */
	if (flags & VMM_PHYSICAL) {
		err = -EINVAL;
		if (!optional)
			goto err_withvma;
		physaddr_t physical = *(physaddr_t*)optional;
		if (physical & (page_size - 1))
			goto err_withvma;
		err = __vmap_physical(flags, mm_struct->pagetable, virtual, physical, prot, page_size, page_count);
		if (err)
			goto err_withvma;
	} else if (flags & VMM_ALLOC) {
		err = __vmap_alloc(flags, mm_struct->pagetable, virtual, prot, page_size, page_count);
		if (err)
			goto err_withvma;
	}

	/* Successful, so now just free previous pages if a fixed/replace, and invalidate TLB's */
	snapshot_cleanup(prev_pages, true);
	tlb_invalidate(virtual, size);
	mutex_release(&mm_struct->mutex);

	*out = virtual;
	return 0;
err_withvma:
	bug(vma_unmap(mm_struct, virtual, size) != 0);
	snapshot_restore_pages(mm_struct, prev_pages);
	tlb_invalidate(virtual, size);
err_novma:
	mutex_release(&mm_struct->mutex);
	return err;
}

static inline bool vprotect_flags_valid(int flags) {
	return (flags == 0 || flags == VMM_USER);
}

static int __vprotect(uintptr_t virtual, size_t size, pgprot_t prot, int flags, void* optional) {
	if (virtual & (PAGE_SIZE - 1) || size == 0 || !vprotect_flags_valid(flags))
		return -EINVAL;

	struct mm* mm_struct = &kernel_mm_struct;
	if (flags & VMM_USER) {
		mm_struct = optional ? ((struct vmm_usermap_info*)optional)->mm_struct : current_cpu()->mm_struct;
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	size_t tlb_flush_round = PAGE_SIZE;
	const uintptr_t start = virtual;
	uintptr_t end;
	if (__builtin_add_overflow(virtual, size, &end))
		return -EINVAL;

	int err = 0;
	mutex_acquire(&mm_struct->mutex);

	struct page_snapshot* prevpages = snapshot_pages(mm_struct, virtual, size);
	if (IS_PTR_ERR(prevpages)) {
		err = PTR_ERR(prevpages);
		goto out;
	}

	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto out;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->vmm_flags & VMM_HUGETLB) {
			bug(!(vma->vmm_flags & VMM_HUGETLB_2MB) || (vma->vmm_flags & VMM_HUGETLB_1GB) || PMD_SIZE != 0x200000);
			page_size = PMD_SIZE;
			tlb_flush_round = PMD_SIZE;

			/* This can only happen if the very first page being remapped is a hugepage */
			if (unlikely(virtual & (page_size - 1))) {
				err = -EINVAL;
				goto out;
			}
		}

		/* Now just update the VMA and pagetable */
		err = vma_protect(mm_struct, virtual, page_size, prot);
		if (err)
			goto out;
		err = arch_pagetable_update(mm_struct->pagetable, virtual,
				arch_pagetable_get_physical(mm_struct->pagetable, virtual),
				!!(vma->vmm_flags & VMM_HUGETLB), prot);
		if (unlikely(err))
			goto out;

		virtual += page_size;
	}

out:
	if (err == 0)
		snapshot_cleanup(prevpages, false);
	else if (!IS_PTR_ERR(prevpages))
		snapshot_restore_pages(mm_struct, prevpages);

	/* Invalidate on both error and no error just in case */
	tlb_invalidate(start, ROUND_UP(size, tlb_flush_round));
	mutex_release(&mm_struct->mutex);
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
		mm_struct = optional ? ((struct vmm_usermap_info*)optional)->mm_struct : current_cpu()->mm_struct;
		if (mm_struct == &kernel_mm_struct)
			return -EINVAL;
	}

	size_t tlb_invalidate_round = PAGE_SIZE;
	const uintptr_t start = virtual;
	uintptr_t end;
	if (__builtin_add_overflow(virtual, size, &end))
		return -EINVAL;

	int err = 0;
	mutex_acquire(&mm_struct->mutex);

	struct page_snapshot* prevpages = snapshot_pages(mm_struct, virtual, size);
	if (IS_PTR_ERR(prevpages)) {
		err = PTR_ERR(prevpages);
		goto out;
	}

	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
		if (!vma) {
			err = -ENOENT;
			goto out;
		}

		size_t page_size = PAGE_SIZE;
		if (vma->vmm_flags & VMM_HUGETLB) {
			bug(!(vma->vmm_flags & VMM_HUGETLB_2MB) || (vma->vmm_flags & VMM_HUGETLB_1GB) || PMD_SIZE != 0x200000);
			page_size = PMD_SIZE;
			tlb_invalidate_round = PMD_SIZE;

			/* Like in vprotect, this can only happen if the very first page is a hugepage */
			if (unlikely(virtual & (page_size - 1))) {
				err = -EINVAL;
				goto out;
			}
		}

		/* Now remove the VMA and mapping in the page table */
		err = vma_unmap(mm_struct, virtual, page_size);
		if (err)
			goto out;
		err = arch_pagetable_unmap(mm_struct->pagetable, virtual);
		if (unlikely(err)) {
			printk(PRINTK_CRIT "mm: Failed to unmap page, err: %i\n", err);
			goto out;
		}

		virtual += page_size;
	}

out:
	if (err == 0)
		snapshot_cleanup(prevpages, true);
	else if (!IS_PTR_ERR(prevpages))
		snapshot_restore_pages(mm_struct, prevpages);

	/* Invalidate on both error and no error just in case */
	tlb_invalidate(start, ROUND_UP(size, tlb_invalidate_round));
	mutex_release(&mm_struct->mutex);
	return err;
}

void* vmap(void* hint, size_t size, pgprot_t prot, int flags, void* optional) {
	if ((flags & VMM_USER) || (flags & VMM_IOMEM))
		return ERR_PTR(-EINVAL);
	uintptr_t ret;
	int err = __vmap((uintptr_t)hint, size, prot, flags, optional, &ret);
	return err ? ERR_PTR(err) : (void*)ret;
}

int vprotect(void* virtual, size_t size, pgprot_t prot, int flags, void* optional) {
	flags &= ~(VMM_ALLOC | VMM_PHYSICAL | VMM_NOREPLACE | VMM_FIXED | VMM_HUGETLB | VMM_HUGETLB_2MB | VMM_HUGETLB_1GB);
	return ((flags & VMM_USER) || (flags & VMM_IOMEM)) ? -EINVAL : __vprotect((uintptr_t)virtual, size, prot, flags, optional);
}

int vunmap(void* virtual, size_t size, int flags, void* optional) {
	flags &= ~(VMM_ALLOC | VMM_PHYSICAL | VMM_NOREPLACE | VMM_FIXED | VMM_HUGETLB | VMM_HUGETLB_2MB | VMM_HUGETLB_1GB);
	return ((flags & VMM_USER) || (flags & VMM_IOMEM)) ? -EINVAL : __vunmap((uintptr_t)virtual, size, flags, optional);
}

void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t cache) {
	if ((!(cache & PGPROT_PCD) && !(cache & PGPROT_PCD)) || (cache & PGPROT_EXEC))
		return NULL;

	const size_t page_offset = physical % PAGE_SIZE;
	const size_t total_size = size + page_offset;

	uintptr_t ret;
	int err = __vmap(0, total_size, cache | PGPROT_READ | PGPROT_WRITE, VMM_IOMEM, &physical, &ret);
	if (err)
		return NULL;

	return (u8 __iomem*)ret + page_offset;
}

int iounmap(void __iomem* virtual, size_t size) {
	size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	const size_t total_size = size + page_offset;
	return __vunmap(ROUND_DOWN((uintptr_t)virtual, PAGE_SIZE), total_size, 0, NULL);
}

void __user* usermap(void __user* hint, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return (__force void __user*)ERR_PTR(-EINVAL);

	prot |= PGPROT_USER;
	flags |= VMM_USER;

	uintptr_t ret;
	int err = __vmap((uintptr_t)hint, size, prot, flags, usermap_info, &ret);
	return err ? (__force void __user*)ERR_PTR(err) : (__force void __user*)ret;
}

int userprotect(void __user* virtual, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return -EINVAL;

	flags &= ~(VMM_ALLOC | VMM_NOREPLACE | VMM_FIXED | VMM_HUGETLB | VMM_HUGETLB_2MB | VMM_HUGETLB_1GB);
	flags |= VMM_USER;
	prot |= PGPROT_USER;

	return __vprotect((uintptr_t)virtual, size, prot, flags, usermap_info);
}

int userunmap(void __user* virtual, size_t size, int flags, struct vmm_usermap_info* usermap_info) {
	if ((flags & VMM_PHYSICAL) || (flags & VMM_IOMEM))
		return -EINVAL;

	flags &= ~(VMM_ALLOC | VMM_NOREPLACE | VMM_FIXED | VMM_HUGETLB | VMM_HUGETLB_2MB | VMM_HUGETLB_1GB);
	flags |= VMM_USER;

	return __vunmap((uintptr_t)virtual, size, flags, usermap_info);
}

static void vmm_init(void) {
	arch_pagetable_init();
	struct cpu* cpu = current_cpu();

	cpu->mm_struct = &kernel_mm_struct;
	struct mm* mm = cpu->mm_struct;
	mutex_init(&mm->mutex);
	mm->pagetable = arch_pagetable_get_cpu_current();
	mm->mmap_start = KERNEL_SPACE_START;
	mm->mmap_end = KERNEL_SPACE_END;
	list_head_init(&mm->vma_list);

	/* 
	 * This is primarily used for giving the HHDM region VMA's.
	 * This ignores the actual kernel sections (KERNEL_SPACE_END doesn't include it)
	 */
	uintptr_t _unused;
	uintptr_t next;
	for (uintptr_t addr = KERNEL_SPACE_START; addr < KERNEL_SPACE_END; addr = next) {
		size_t page_size = arch_pagetable_iterate_range(mm->pagetable, addr, &next);
		if (page_size != 0)
			bug(vma_map(mm, addr, page_size, PGPROT_READ | PGPROT_WRITE, VMM_FIXED | VMM_NOREPLACE | VMM_SEALED, &_unused) != 0);
	}
}

static void vmm_ap_init(void) {
	struct cpu* cpu = current_cpu();
	cpu->mm_struct = &kernel_mm_struct;
	arch_pagetable_switch(cpu->mm_struct->pagetable);
}

INIT_TASK_DECLARE(vma_init_task, hhdm_init_task, zones_init_task);
INIT_TASK_DEFINE(vmm_init_task, INIT_TASK_SCOPE_BSP, vmm_init, &vma_init_task, &hhdm_init_task, &zones_init_task);
INIT_TASK_DEFINE(vmm_ap_init_task, INIT_TASK_SCOPE_AP, vmm_ap_init, &vmm_init_task);
