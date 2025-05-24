#include <crescent/common.h>
#include <crescent/compiler.h>
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

#define PML4_MAX_4K_PAGES 0x8000000ul
#define PTE_COUNT 512

static struct vma* kvma;
static struct vma* iovma;
static spinlock_t kernel_pt_lock = SPINLOCK_INITIALIZER;

static unsigned long mmu_to_pt(unsigned long mmu_flags) {
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

static int __iomap(void __iomem* virtual, physaddr_t physical,
		unsigned long pt_flags, unsigned long page_count) {
	struct vmm_ctx* vmm_ctx = &current_cpu()->vmm_ctx;

	unsigned long pages_mapped = 0;
	while (page_count--) {
		int err = pagetable_map(vmm_ctx->pagetable, (void*)virtual, physical, pt_flags);
		if (err) {
			while (pages_mapped--) {
				virtual = (u8*)virtual - PAGE_SIZE;
				pagetable_unmap(vmm_ctx->pagetable, (void*)virtual);
				tlb_flush_single((void*)virtual);
			}

			return err;
		}

		pages_mapped++;
		virtual = (u8*)virtual + PAGE_SIZE;
		physical += PAGE_SIZE;
	}

	return 0;
}

static int __iounmap(void __iomem* virtual, unsigned long page_count) {
	struct vmm_ctx* vmm_ctx = &current_cpu()->vmm_ctx;

	unsigned long pages_unmapped = 0;
	while (page_count--) {
		int err = pagetable_unmap(vmm_ctx->pagetable, (void*)virtual);
		if (err) {
			if (pages_unmapped != 0) {
				printk(PRINTK_CRIT "mm: failed to unmap all pages\n");
				dump_stack(); /* Could be the result of a programming error */
			}
			return err;
		}
	}

	return 0;
}

static int __kmap(void* virtual, mm_t mm_flags, unsigned long pt_flags, unsigned long page_count) {
	struct vmm_ctx* vmm_ctx = &current_cpu()->vmm_ctx;

	unsigned long pages_mapped = 0;
	while (page_count--) {
		physaddr_t mem = alloc_page(mm_flags);
		if (!mem)
			break;
		int err = pagetable_map(vmm_ctx->pagetable, virtual, mem, pt_flags);
		if (err) {
			while (pages_mapped--) {
				virtual = (u8*)virtual - PAGE_SIZE;
				physaddr_t physical = pagetable_get_physical(vmm_ctx->pagetable, virtual);
				free_page(physical);
				pagetable_unmap(vmm_ctx->pagetable, virtual);
				tlb_flush_single(virtual);
			}
		}

		tlb_flush_single(virtual);

		pages_mapped++;
		virtual = (u8*)virtual + PAGE_SIZE;
	}

	return 0;
}

static int __kprotect(void* virtual, unsigned long pt_flags, unsigned long page_count) {
	struct vmm_ctx* vmm_ctx = &current_cpu()->vmm_ctx;

	unsigned long pages_remapped = 0;
	while (page_count) {
		physaddr_t mem = pagetable_get_physical(vmm_ctx->pagetable, virtual);
		int err = pagetable_update(vmm_ctx->pagetable, virtual, mem, pt_flags);
		if (err) {
			if (pages_remapped == 0)
				return err;

			printk(PRINTK_CRIT "mm: Failed to remap all pages, entries are unreliable!\n");
			dump_stack();
			return err;
		}

		tlb_flush_single(virtual);

		page_count--;
		pages_remapped++;

		virtual = (u8*)virtual + PAGE_SIZE;
	}

	return 0;
}

static int __kunmap(void* virtual, unsigned long page_count) {
	struct vmm_ctx* vmm_ctx = &current_cpu()->vmm_ctx;

	while (page_count--) {
		physaddr_t mem = pagetable_get_physical(vmm_ctx->pagetable, virtual);
		if (mem)
			free_page(mem);
		else
			printk(PRINTK_CRIT "mm: Virtual address not mapped to anything!\n");

		int err = pagetable_unmap(vmm_ctx->pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "mm: Failed to unmap kernel page, err: %i\n", err);
			return err;
		}

		tlb_flush_single(virtual);
		virtual = (u8*)virtual + PAGE_SIZE;
	}

	return 0;
}

static inline int in_vma(struct vma* vma, const void* virtual, size_t size) {
	uintptr_t virtual_top;
	if (__builtin_add_overflow((uintptr_t)virtual, size, &virtual_top))
		return -ERANGE;
	else if (virtual < vma->start || virtual_top > (uintptr_t)vma->end)
		return -EADDRNOTAVAIL;
	
	return 0;
}

void __iomem* iomap(physaddr_t physical, size_t size, unsigned long mmu_flags) {
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == 0)
		return NULL;

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/* TODO: Do not map IO memory to the same VMA as the kernel VMA */
	void __iomem* virtual = vma_alloc_pages(iovma, page_count, PAGE_SIZE);
	if (!virtual)
		return NULL;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	int err = __iomap(virtual, physical, pt_flags, page_count);
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	if (err) {
		vma_free_pages(iovma, (void*)virtual, page_count);
		return NULL;
	}

	return virtual;
}

int iounmap(void __iomem* virtual, size_t size) {
	int err = in_vma(iovma, (void*)virtual, size);
	if (err) {
		if (err == -ERANGE)
			printk(PRINTK_ERR "mm: virtual + size overflows!\n");
		else if (err == -EADDRNOTAVAIL)
			printk(PRINTK_ERR "mm: virtual not in kernel VMA!\n");
		dump_stack();
		return err;
	}

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long lock_flags;

	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	err = __iounmap(virtual, page_count);
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	if (err)
		return err;

	vma_free_pages(iovma, (void*)virtual, page_count);
	return 0;
}

void* kmap(mm_t mm_flags, size_t size, unsigned long mmu_flags) {
	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == 0)
		return NULL;

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	void* virtual = vma_alloc_pages(kvma, page_count, PAGE_SIZE);
	if (!virtual)
		return NULL;

	unsigned long lock_flags;

	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	int err = __kmap(virtual, mm_flags, pt_flags, page_count);
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	if (err) {
		vma_free_pages(kvma, virtual, page_count);
		return NULL;
	}

	return virtual;
}

int kprotect(void* virtual, size_t size, unsigned long mmu_flags) {
	int err = in_vma(kvma, virtual, size);
	if (err) {
		if (err == -ERANGE)
			printk(PRINTK_ERR "mm: virtual + size overflows!\n");
		else if (err == -EADDRNOTAVAIL)
			printk(PRINTK_ERR "mm: virtual not in kernel VMA!\n");
		dump_stack();
		return err;
	}

	unsigned long pt_flags = mmu_to_pt(mmu_flags);
	if (pt_flags == 0)
		return -EINVAL;

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	err = __kprotect(virtual, pt_flags, page_count);
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	return err;
}

int kunmap(void* virtual, size_t size) {
	int err = in_vma(kvma, virtual, size);
	if (err) {
		if (err == -ERANGE)
			printk(PRINTK_ERR "mm: virtual + size overflows!\n");
		else if (err == -EADDRNOTAVAIL)
			printk(PRINTK_ERR "mm: virtual not in kernel VMA!\n");
		dump_stack();
		return err;
	}

	unsigned long page_count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long lock_flags;

	spinlock_lock_irq_save(&kernel_pt_lock, &lock_flags);
	err = __kunmap(virtual, page_count);
	spinlock_unlock_irq_restore(&kernel_pt_lock, &lock_flags);
	if (err)
		return err;

	vma_free_pages(kvma, virtual, page_count);
	return 0;
}

void vmm_init(void) {
	physaddr_t _cr3;
	__asm__("movq %%cr3, %0" : "=r"(_cr3));
	pte_t* cr3 = hhdm_virtual(_cr3);

	current_cpu()->vmm_ctx.pagetable = cr3;

	void* kvma_start = NULL;
	void* kvma_end = NULL;
	void* iovma_start = NULL;
	void* iovma_end = NULL;

	for (unsigned long entry = 256; entry < PTE_COUNT; entry++) {
		if (cr3[entry] & PT_PRESENT)
			continue;

		/* The end of the VMA is subtracted by a page size to force room for a page fault */
		void* start = (void*)((entry << 39) | 0xFFFF000000000000);
		void* end = (u8*)start + (PML4_MAX_4K_PAGES * PAGE_SIZE) - PAGE_SIZE;
	
		if (!kvma_start) {
			kvma_start = start;
			kvma_end = end;
		} else if (!iovma_start) {
			iovma_start = start;
			iovma_end = end;
		} else {
			break;
		}
	}

	if (unlikely(!kvma_start || !iovma_end))
		panic("No PT entry for kvma or iovma!");

	kvma = vma_create(kvma_start, kvma_end, 10);
	iovma = vma_create(iovma_start, iovma_end, 10);
	if (!kvma || !iovma)
		panic("Failed to create kvma or iovma structure!");
}
