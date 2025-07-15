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

/**
 * @brief Map an entry into a page table
 *
 * -EINVAL is returned if virtual or physical are not page aligned.
 * Same goes with invalid pt_flags. This value is also returned if virtual is non-canonical,
 * or if physical is NULL.
 *
 * -EEXIST is returned if the PTE is already present.
 *
 * -ENOMEM is returned if no page tables could be allocated for the new mapping
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to map, must be page aligned
 * @param physical The physical address to map the virtual address to, must be page aligned, cannot be 0
 * @param pt_flags The page table flags to use
 *
 * @return -errno on failure
 */
int pagetable_map(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags);

/**
 * @brief Update an entry in a page table
 *
 * -EINVAL is returned if virtual or physical are not page aligned.
 * Same goes with invalid pt_flags. This value is also returned if virtual is non-canonical,
 * or if physical is NULL.
 * 
 * -EFAULT is returned if the page sizes don't match (eg. trying to remap a 2MiB page to a 4K one)
 *
 * -ENOENT is returned if the PTE isn't present.
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to map, must be page aligned
 * @param physical The physical address to map, must be page aligned, cannot be 0
 * @param pt_flags The PT flags to use
 *
 * @return -errno on failure
 */
int pagetable_update(pte_t* pagetable, void* virtual, physaddr_t physical, unsigned long pt_flags);

/**
 * @brief Unmap an entry in a page table
 *
 * -EINVAL is returned if virtual isn't page aligned. This value is also returned if virtual is non-canonical.
 * -ENOENT is returned if the PTE isn't present
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address to unmap, must be page aligned
 */
int pagetable_unmap(pte_t* pagetable, void* virtual);

/**
 * @brief Get the physical address of a mapping in a page table
 *
 * Returns NULL if the physical address isn't mapped.
 *
 * @param pagetable The page table to use
 * @param virtual The virtual address, does not need to be page aligned
 *
 * @return The physical address of the mapping
 */
physaddr_t pagetable_get_physical(pte_t* pagetable, const void* virtual);

/**
 * @brief Get the base address of a top level page table index
 * @param index The index
 * @return The base address
 */
void* pagetable_get_base_address_from_top_index(unsigned int index);

void pagetable_init(void);
