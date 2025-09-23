#include <lunar/common.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/cpuid.h>
#include <lunar/compiler.h>
#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/core/limine.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/lib/string.h>
#include "internal.h"

#define HUGEPAGE_1G 0x40000000

static inline pte_t* table_virtual(pte_t entry) {
	entry &= ~(0xFFF | PT_NX);
	return hhdm_virtual((physaddr_t)entry);
}

static inline bool is_virtual_canonical(const void* virtual) {
	return ((uintptr_t)virtual >> 47 == 0 || (uintptr_t)virtual >> 47 == 0x1FFFF);
}

static inline void pagetable_get_indexes(const void* virtual, unsigned int* indexes) {
	indexes[0] = (uintptr_t)virtual >> 39 & 0x01FF;
	indexes[1] = (uintptr_t)virtual >> 30 & 0x01FF;
	indexes[2] = (uintptr_t)virtual >> 21 & 0x01FF;
	indexes[3] = (uintptr_t)virtual >> 12 & 0x01FF;
}

unsigned long pagetable_mmu_to_pt(mmuflags_t mmu_flags) {
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

static int walk_pagetable(pte_t* pagetable, const void* virtual, bool create, size_t* page_size, pte_t** ret) {
	*ret = NULL;

	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes);

	/* 
	 * Keeps track of what page tables were allocated and what PTE they are in, 
	 * so proper cleanup can be done after any allocation failures
	 */
	physaddr_t new_tables[3] = { 0, 0, 0 };
	pte_t* new_tables_table[3] = { NULL, NULL, NULL };

	for (size_t i = 0; i < ARRAY_SIZE(indexes) - 1; i++) {
		/* Check to see if we want either a 1GiB or 2MiB page */
		if ((*page_size == HUGEPAGE_1G && i == 1) || (*page_size == HUGEPAGE_2M_SIZE && i == 2)) {
			*ret = &pagetable[indexes[i]];
			return 0;
		}

		/* 
		 * If not present, a new page table needs to be allocated. 
		 * Otherwise, handle the case where it could be a hugepage
		 */
		if (!(pagetable[indexes[i]] & PT_PRESENT)) {
			if (!create)
				return -ENOENT;
			physaddr_t new = alloc_page(MM_ZONE_NORMAL);
			if (!new) {
				/* Clean up all the new page tables that were allocated */
				for (size_t j = 0; j < ARRAY_SIZE(new_tables); j++) {
					if (new_tables[j]) {
						free_page(new_tables[j]);
						*new_tables_table[j] = 0;
					}
				}
				return -ENOMEM;
			}

			memset(hhdm_virtual(new), 0, PAGE_SIZE);

			/* Update the PTE, and store the pointers for cleanup on failure */
			new_tables[i] = new;
			pagetable[indexes[i]] = new | PT_PRESENT | PT_READ_WRITE;
			new_tables_table[i] = &pagetable[indexes[i]];
		} else if (pagetable[indexes[i]] & PT_HUGEPAGE) {
			if (unlikely(i != 1 && i != 2))
				panic("Hugepage flag set on invalid PTE!");

			/* 
			 * Make sure we're not returning a PTE that points to another page table, 
			 * a *page_size of zero means that the caller doesn't care if it's a hugepage or not.
			 *
			 * If *page_size is zero, then write the page size so that way the caller knows 
			 * if it needs it for some reason.
			 */
			size_t _page_size = i == 1 ? HUGEPAGE_1G : HUGEPAGE_2M_SIZE;
			if (_page_size != *page_size) {
				if (*page_size != 0)
					return -EEXIST;
				*page_size = _page_size;
			}

			*ret = &pagetable[indexes[i]];
			return 0;
		}

		pagetable = table_virtual(pagetable[indexes[i]]);
	}

	*page_size = PAGE_SIZE;
	*ret = &pagetable[indexes[3]];
	return 0;
}

int pagetable_map(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags) {
	size_t page_size = pt_flags & PT_HUGEPAGE ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	if ((uintptr_t)virtual & (page_size - 1) || physical & (page_size - 1) || 
			!is_virtual_canonical(virtual) || !physical)
		return -EINVAL;

	pte_t* pte;
	int err = walk_pagetable(pagetable, virtual, true, &page_size, &pte);
	if (err)
		return err;

	if (*pte)
		return -EEXIST;

	*pte = physical | pt_flags;
	return 0;
}

int pagetable_update(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags) {
	if (!is_virtual_canonical(virtual) || !physical)
		return -EINVAL;

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, &page_size, &pte);
	if (err)
		return err;

	if ((pt_flags & PT_HUGEPAGE && page_size != HUGEPAGE_2M_SIZE) ||
			(!(pt_flags & PT_HUGEPAGE) && page_size != PAGE_SIZE))
		return -EFAULT;
	if ((uintptr_t)virtual & (page_size - 1) || physical & (page_size - 1))
		return -EINVAL;

	*pte = physical | pt_flags;
	return 0;
}

static void pagetable_cleanup(pte_t* pagetable, void* virtual) {
	unsigned int indexes[4];
	pagetable_get_indexes(virtual, indexes);

	pte_t* tables[4] = { pagetable, NULL, NULL, NULL };
	for (unsigned int i = 0; i < ARRAY_SIZE(tables) - 1; i++) {
		pte_t entry = tables[i][indexes[i]];
		if (!(entry & PT_PRESENT) || entry & PT_HUGEPAGE)
			return;
		tables[i + 1] = table_virtual(entry);
	}

	for (unsigned int level = 3; level > 0; level--) {
		pte_t* table = tables[level];
		for (unsigned int i = 0; i < 512; i++) {
			if (table[i] & PT_PRESENT)
				return;
		}

		physaddr_t physical = tables[level - 1][indexes[level - 1]] & ~(0xFFF | PT_NX);
		free_page(physical);
		tables[level - 1][indexes[level - 1]] = 0;
	}
}

int pagetable_unmap(pte_t* pagetable, void* virtual) {
	if (!is_virtual_canonical(virtual))
		return -EINVAL;

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, &page_size, &pte);
	if (err)
		return err;
	if ((uintptr_t)virtual & (page_size - 1))
		return -EINVAL;

	if (!(*pte))
		return -ENOENT;

	*pte = 0;
	pagetable_cleanup(pagetable, virtual);

	return 0;
}

physaddr_t pagetable_get_physical(pte_t* pagetable, const void* virtual) {
	if (!is_virtual_canonical(virtual))
		return 0;

	pte_t* pte;
	size_t page_size = 0;
	int err = walk_pagetable(pagetable, virtual, false, &page_size, &pte);
	if (err)
		return 0;

	if (!(*pte))
		return 0;

	return (*pte & ~(0xFFF | PT_NX)) + ((uintptr_t)virtual & (page_size - 1));
}

static struct limine_paging_mode_request __limine_request paging_mode = {
	.request.id = LIMINE_PAGING_MODE_REQUEST,
	.request.revision = 1,
	.min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
	.max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
	.mode = LIMINE_PAGING_MODE_X86_64_4LVL,
	.response = NULL
};

#define PML4_MAX_4K_PAGES 0x8000000ul

void* pagetable_get_base_address_from_top_index(unsigned int index) {
	if (index >= 512)
		return NULL;
	if (index >= 256)
		return (void*)(((u64)index << 39) | 0xFFFF000000000000);
	return (void*)((u64)index << 39);
}

void pagetable_init(void) {
	u32 ecx, _unused;
	cpuid(0x07, 0, &_unused, &_unused, &ecx, &_unused);

	/* bit 16 being set means the CPU supports level 5 paging */
	bool level4 = ecx & (1 << 16) ? !(ctl4_read() & CTL4_LA57) : true;
	if (!level4)
		panic("Bootloader selected wrong paging mode!\n");
}
