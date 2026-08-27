#pragma once

#include <lunar/types.h>

#define ARCH_KERNEL_SPACE_START 0xffff800000000000
#define ARCH_KERNEL_SPACE_END 0xffffffff80000000
#define ARCH_KERNEL_SECTIONS_SPACE_START 0xffffffff80000000
#define ARCH_KERNEL_SECTIONS_SPACE_END 0xffffffffffffffff
#define ARCH_USER_SPACE_START 0x0000000000000000
#define ARCH_USER_SPACE_END 0x0000800000000000

#define ARCH_PAGE_SHIFT 12
#define ARCH_PMD_SHIFT 21
#define ARCH_PUD_SHIFT 30

typedef unsigned long arch_pte_t;
typedef enum {
	ARCH_PTE_FLAG_NONE = 0,
	ARCH_PTE_FLAG_READ = (1 << 0),
	ARCH_PTE_FLAG_WRITE = (ARCH_PTE_FLAG_READ | (1 << 1)),
	ARCH_PTE_FLAG_USER = (1 << 2),
	ARCH_PTE_FLAG_WT = (1 << 3),
	ARCH_PTE_FLAG_UC = (1 << 4),
	ARCH_PTE_FLAG_WC = (1 << 5), /* Must be handled using the PAT */
	ARCH_PTE_FLAG_EXEC = (1 << 6), /* Must also be handled differently, since NX is used */
	ARCH_PTE_FLAG_HUGETLB_2MB = (1 << 7), /* Also must be handled differently because of 1GB pages, but happens to be the same as the hugepage bit in the PTE entry */
	ARCH_PTE_FLAG_HUGETLB_1GB = (1 << 8)
} arch_pte_flags_t;

void arch_pagetable_init(void);
void arch_pagetable_ap_init(void);
arch_pte_t* arch_pagetable_new(void);
void arch_pagetable_free(arch_pte_t* table);
int arch_pagetable_map(arch_pte_t* pagetable, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags);
int arch_pagetable_update(arch_pte_t* pagetable, uintptr_t virtual, physaddr_t physical, arch_pte_flags_t pte_flags);
int arch_pagetable_unmap(arch_pte_t* pagetable, uintptr_t virtual);
int arch_pagetable_get_physical(arch_pte_t* pagetable, uintptr_t virtual, physaddr_t* out);
arch_pte_t* arch_pagetable_get_cpu_current(void);
void arch_pagetable_switch(arch_pte_t* pagetable);
size_t arch_pagetable_iterate_range(arch_pte_t* pagetable, uintptr_t virtual, uintptr_t* next);
bool arch_supports_page_size(size_t page_size);
