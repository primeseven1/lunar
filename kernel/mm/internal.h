#pragma once

#include <lunar/mm.h>

struct vma {
	uintptr_t start, top;
	pgprot_t prot;
	int vmm_flags;
	struct list_node link;
};

/**
 * @brief Free all VMA's in a list
 * @param list The start of the list
 */
void vma_destroy(struct list_head* list);

/**
 * @brief Find a VMA
 *
 * @param mm The mm struct
 * @param address The virtual address
 *
 * @return The address of the VMA struct, NULL if not found
 */
struct vma* vma_find(struct mm* mm, uintptr_t address);

/**
 * @brief Map a virtual address range
 *
 * @param[in] mm The mm struct
 * @param[in] hint Hint on where to place the mapping
 * @param[in] prot Protection flags
 * @param[in] vmm_flags VMM flags
 * @param[out] Where the address of the mapping is
 *
 * @return -errno on failure, 0 on success
 */
int vma_map(struct mm* mm, uintptr_t hint, size_t size, pgprot_t prot, int vmm_flags, uintptr_t* ret);

/**
 * @brief Protect a virtual address range
 *
 * @param mm The mm struct to use
 * @param address The address to protect
 * @param size The size to protect
 * @param prot New protection flags
 *
 * @return -errno on failure, 0 on success
 */
int vma_protect(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot);

/**
 * @brief Unmap a virtual address range
 *
 * @param mm The mm struct to use
 * @param address The address to unmap
 * @param size The size to unmap
 *
 * @return -errno on failure, 0 on success
 */
int vma_unmap(struct mm* mm, uintptr_t address, size_t size);

#define TLB_BATCH_PAGE_COUNT 32

struct tlb_batch {
	pte_t* pagetable;
	uintptr_t first_page_virtual, last_page_virtual;
	size_t page_count; /* Number of pages in the pages array */
	struct page* pages[TLB_BATCH_PAGE_COUNT]; /* Since multiple addresses may map to the same page, we cannot use a list here */
};

/**
 * @brief Initialize a TLB batch structure
 *
 * @param batch The batch to initialize
 * @param pagetable The page table
 */
void tlb_batch_init(struct tlb_batch* batch, pte_t* pagetable);

/**
 * @brief Flush TLB entries for a TLB batch structure
 *
 * After this function, any page structures associated with this batch will be released.
 * Not safe to call from an atomic context, as this may acquire mutexes.
 *
 * @param batch The batch to flush
 */
void tlb_batch_flush(struct tlb_batch* batch);

/**
 * @brief Add a page to a TLB batch
 *
 * If the number of pages exceeds TLB_BATCH_PAGE_COUNT, this function will call
 * tlb_batch_flush() to allow more pages to be added.
 *
 * @param batch The batch to add to
 * @param virtual The virtual address of the page
 * @param page The page to release after flushing (optional)
 */
void tlb_batch_add(struct tlb_batch* batch, uintptr_t virtual, struct page* page);
