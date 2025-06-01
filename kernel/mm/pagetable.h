#pragma once

#include <crescent/mm/vmm.h>

#define PTE_COUNT 512

enum pt_flags {
	PT_PRESENT = (1 << 0),
	PT_READ_WRITE = (1 << 1),
	PT_USER_SUPERVISOR = (1 << 2),
	PT_WRITETHROUGH = (1 << 3),
	PT_CACHE_DISABLE = (1 << 4),
	PT_ACCESSED = (1 << 5),
	PT_DIRTY = (1 << 6),
	PT_4K_PAT = (1 << 7),
	PT_HUGEPAGE = (1 << 7),
	PT_GLOBAL = (1 << 8),
	PT_HUGEPAGE_PAT = (1 << 12),
	PT_NX = (1ul << 63)
};

int pagetable_map(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags);
int pagetable_update(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags);
int pagetable_unmap(pte_t* pagetable, void* virtual);
physaddr_t pagetable_get_physical(pte_t* pagetable, const void* virtual);

void* pagetable_get_base_address_from_top_index(unsigned int index);
void* pagetable_get_end_address_from_top_index(unsigned int index);
void pagetable_init(void);
