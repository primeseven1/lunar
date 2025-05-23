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

static int __kmap(void* virtual, mm_t mm_flags, unsigned long pt_flags, unsigned long page_count) {
	struct cpu* cpu = current_cpu();
	int err = 0;

	unsigned long pages_mapped = 0;
	while (page_count) {
		physaddr_t mem = alloc_page(mm_flags);
		if (!mem)
			break;
		err = pagetable_map(cpu->vmm_ctx.pagetable, virtual, mem, pt_flags);
		if (err)
			break;
		tlb_flush_single(virtual);

		pages_mapped++;
		page_count--;

		virtual = (u8*)virtual + PAGE_SIZE;
	}

	if (page_count == 0)
		return 0;

	while (pages_mapped--) {
		virtual = (u8*)virtual - PAGE_SIZE;
		physaddr_t physical = pagetable_get_physical(cpu->vmm_ctx.pagetable, virtual);
		free_page(physical);
		pagetable_unmap(cpu->vmm_ctx.pagetable, virtual);
		tlb_flush_single(virtual);
	}

	return err;
}

static int __kprotect(void* virtual, unsigned long pt_flags, unsigned long page_count) {
	struct cpu* cpu = current_cpu();

	unsigned long pages_remapped = 0;
	while (page_count) {
		physaddr_t mem = pagetable_get_physical(cpu->vmm_ctx.pagetable, virtual);
		int err = pagetable_update(cpu->vmm_ctx.pagetable, virtual, mem, pt_flags);
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
	struct cpu* cpu = current_cpu();

	while (page_count--) {
		physaddr_t mem = pagetable_get_physical(cpu->vmm_ctx.pagetable, virtual);
		if (mem)
			free_page(mem);
		else
			printk(PRINTK_CRIT "mm: Virtual address not mapped to anything!\n");

		int err = pagetable_unmap(cpu->vmm_ctx.pagetable, virtual);
		if (err) {
			printk(PRINTK_CRIT "mm: Failed to unmap kernel page, err: %i\n", err);
			return err;
		}

		tlb_flush_single(virtual);
		virtual = (u8*)virtual + PAGE_SIZE;
	}

	return 0;
}

static inline int in_kernel_vma(void* virtual, size_t size) {
	uintptr_t virtual_top;
	if (__builtin_add_overflow((uintptr_t)virtual, size, &virtual_top))
		return -ERANGE;
	else if (virtual < kvma->start || virtual_top > (uintptr_t)kvma->end)
		return -EADDRNOTAVAIL;
	
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
	int err = in_kernel_vma(virtual, size);
	if (err == -ERANGE) {
		printk(PRINTK_ERR "mm: virtual + size overflows!\n");
		dump_stack();
	} else if (err == -EADDRNOTAVAIL) {
		printk(PRINTK_ERR "mm: virtual not in kernel VMA!\n");
		dump_stack();
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
	int err = in_kernel_vma(virtual, size);
	if (err == -ERANGE) {
		printk(PRINTK_ERR "mm: virtual + size overflows!\n");
		dump_stack();
	} else if (err == -EADDRNOTAVAIL) {
		printk(PRINTK_ERR "mm: virtual not in kernel VMA!\n");
		dump_stack();
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

	unsigned long entry = 0;
	for (entry = 256; entry < PTE_COUNT; entry++) {
		if (!(cr3[entry] & PT_PRESENT))
			break;
	}

	if (unlikely(entry == 0))
		panic("Cannot initialize vmm: all level 4 entries occupied?");

	void* start = (void*)((entry << 39) | 0xFFFF000000000000);
	void* end = (u8*)start + PML4_MAX_4K_PAGES * PAGE_SIZE;
	kvma = vma_create(start, end, 10);
	if (unlikely(!kvma))
		panic("Cannot initialize vmm: cannot create kernel VMA");
}
