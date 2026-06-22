#pragma once

#include <lunar/types.h>
#include <arch/asm/errno.h>

#if !defined(KERNEL_SPACE_START) && !defined(KERNEL_SPACE_END) && !defined(KERNEL_SECTIONS_SPACE_START) && !defined(KERNEL_SECTIONS_SPACE_END)
#define KERNEL_SPACE_START 0xffff800000000000
#define KERNEL_SPACE_END 0xffffffff80000000
#define KERNEL_SECTIONS_SPACE_START 0xffffffff80000000
#define KERNEL_SECTIONS_SPACE_END 0xffffffffffffffff
#endif /* !defined(KERNEL_SPACE_START) && !defined(KERNEL_SPACE_END) && !defined(KERNEL_SECTIONS_SPACE_START) && !defined(KERNEL_SECTIONS_SPACE_END) */

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#define PAGE_SIZE (1u << PAGE_SHIFT)
#endif /* PAGE_SHIFT */

#ifndef PMD_SHIFT
#define PMD_SHIFT 21
#define PMD_SIZE (1ul << PMD_SHIFT)
#endif /* PMD_SHIFT */

#ifndef ARCH_GENERIC_PAGE_H_OVERRIDE_TYPES

typedef unsigned long pte_t;

typedef enum {
	PGPROT_NONE = 0, /* Inaccessible */
	PGPROT_READ = (1 << 0), /* Readable */
	PGPROT_WRITE = (1 << 1), /* Writable */
	PGPROT_USER = (1 << 2), /* User accessible */
	PGPROT_PWT = (1 << 3), /* Write-through */
	PGPROT_PCD = (1 << 4), /* Cache disabled */
	PGPROT_EXEC = (1 << 5), /* page is executable */
} pgprot_t;

#endif /* ARCH_GENERIC_PAGE_H_OVERRIDE_TYPES */

#ifndef PGPROT_MASK
#define PGPROT_MASK (PGPROT_NONE | PGPROT_READ | PGPROT_WRITE | PGPROT_USER | PGPROT_PWT | PGPROT_PCD | PGPROT_EXEC)
#endif /* PGPROT_MASK */

/* Architecture must provide these functions */

void arch_pagetable_init(void);

/**
 * @brief Map a page into a page table
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address, should be aligned
 * @param physical The physical address to map the virtual address to, must be aligned
 * @param hugetlb Whether or not the page is a hugepage
 * @param prot Protection flags
 *
 * @retval -EEXIST Mapping already exists in page table
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL Misaligned virtual or physical address, or invalid prot
 * @retval 0 Successful
 */
int arch_pagetable_map(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, bool hugetlb, pgprot_t prot);

/**
 * @brief Update a page table entry
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address of the page
 * @param physical The physical address to map the virtual address to, must be aligned
 * @param hugetlb The page being updated use a hugepage
 * @param prot Protection flags
 *
 * @retval -ENOENT Page table entry doesn't exist
 * @retval -EFAULT Mismatch between the hugetlb parameter and the actual page size
 * @retval -EINVAL virtual or physical aren't properly aligned
 * @retval 0 Successful
 */
int arch_pagetable_update(pte_t* pagetable, uintptr_t virtual, physaddr_t physical, bool hugetlb, pgprot_t prot);

/**
 * @brief Unmap a virtual address in a page table
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to unmap
 *
 * @retval -EINVAL Virtual address isn't aligned with page size
 * @retval -ENOENT Virtual address isn't mapped
 * @retval 0 Successful
 */
int arch_pagetable_unmap(pte_t* pagetable, uintptr_t virtual);

/**
 * @brief Get the physical address from a virtual address
 *
 * This function supports ANY page size the processor supports,
 * even if the kernel doesn't support mapping these pages.
 *
 * @param pagetable The pagetable to use
 * @param virtual The virtual address, can be misaligned
 *
 * @return The physical address of the page including the alignment. Returns 0 for unmapped.
 */
physaddr_t arch_pagetable_get_physical(pte_t* pagetable, uintptr_t virtual);

/**
 * @brief Get the current CPU's page table pointer
 * @return The virtual address of the page table
 */
pte_t* arch_pagetable_get_cpu_current(void);

/**
 * @brief Switch to a new page table
 *
 * The page table is a HHDM pointer.
 *
 * @param pagetable The page table to switch to
 */
void arch_pagetable_switch(pte_t* pagetable);

/**
 * @brief Iterate a virtual address range
 *
 * @param[in] pagetable The page table to use
 * @param[in] virtual The virtual address to start at
 * @param[out] next Where to continue scanning
 *
 * @return 0 If there is no page(s) at the virtual address, otherwise it returns the page size
 */
size_t arch_pagetable_iterate_range(pte_t* pagetable, uintptr_t virtual, uintptr_t* next);

/**
 * @brief Initialize page tables
 *
 * This function creates all root-level higher half page tables.
 * Those page tables must not be freed.
 */
void arch_pagetable_init(void);
