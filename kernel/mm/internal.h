#pragma once

#include <lunar/vmm.h>
#include <lunar/rbtree.h>

#define VM_AREA_PERSISTENT_FLAGS (VMM_ALLOC | VMM_SEALED | VMM_STACK | VMM_IOMEM)

struct vm_area {
	uintptr_t start, end;
	size_t page_size;
	pgprot_t prot;
	int vmm_flags;
	struct rbtree_node rbtree_link;
	struct list_node list_link;
};

#define TLB_BATCH_PAGE_COUNT 32

struct tlb_batch {
	pte_t* pagetable;
	uintptr_t first_page_virtual, last_page_virtual;
	size_t page_count; /* Number of pages in the pages array */
	struct page* pages[TLB_BATCH_PAGE_COUNT]; /* Since multiple addresses may map to the same page, we cannot use a list here */
};

/**
 * @brief Cover a virtual address range with a VMA
 *
 * @param[in] mm The mm struct to use
 * @param[in] hint Hint on where to place the mapping
 * @param[in] size The size of the mapping
 * @param[in] prot Page protection flags
 * @param[in] vmm_flags VMM_* flags
 * @param[out] out Return value for the address of the mapping
 *
 * @retval -EINVAL Invalid hint (when VMM_FIXED is used) or invalid flag combination
 * @retval -ERANGE hint + size overflows, or size cannot be rounded to a page size, this value should NOT be returned to userspace (return -EINVAL)
 * @retval -ENOMEM Out of virtual memory address space, or out of physical memory
 * @retval -EEXIST VMM_FIXED and VMM_NOREPLACE was used, but a VMA already exists at the hint
 * @retval -EPERM VMM_FIXED was used, but the VMA already exists AND the VMA has VMM_SEALED applied to it
 * @retval 0 Successful
 */
int vma_map(struct mm* mm, uintptr_t hint, size_t size, pgprot_t prot, int vmm_flags, uintptr_t* out);

/**
 * @brief Update a VMA
 *
 * @param mm The mm struct to use
 * @param address The address to update
 * @param size The size to update
 * @param prot New protection flags
 * @param vmm_flags VMM_* flags
 *
 * @retval -ENOMEM Out of memory, or there is a hole in between address + size
 * @retval -EINVAL Invalid flags
 * @retval -ERANGE address + size overflows, or the size cannot be rounded to a page size, should not be returned to userspace
 * @retval -EPERM The VMA as VMM_SEALED applied to it
 * @retval 0 Successful
 */
int vma_update(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot, int vmm_flags);

/**
 * @brief Unmap a VMA
 *
 * @param mm The mm struct to use
 * @param address The address to update
 * @param size The size to update
 * @param vmm_flags VMM_* flags
 *
 * @retval -EINVAL Invalid flags
 * @retval -ERANGE address + size overflows, or size could not be rounded to a page size
 * @retval -ENOMEM Out of memory (can happen when splitting a VMA)
 * @retval -EPERM A VMA in the range has VMM_SEALED applied to it
 */
int vma_unmap(struct mm* mm, uintptr_t address, size_t size, int vmm_flags);

/**
 * @brief Look up a VMA by address
 *
 * @param mm The mm struct to use
 * @param address The address to look up
 *
 * @return The pointer to the vm area
 */
struct vm_area* vma_lookup(struct mm* mm, uintptr_t address);

/**
 * @brief Free all VMA's in a list
 * @param vma_list The VMA list
 */
void vma_destroy(struct list_head* vma_list);

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
