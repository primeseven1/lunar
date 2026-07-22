#pragma once

#include <lunar/mm.h>

struct vma {
	uintptr_t start, top;
	pgprot_t prot;
	int vmm_flags;
	struct list_node link;
};

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

struct page_snapshot {
	uintptr_t start;
	physaddr_t physical;
	size_t page_size;
	pgprot_t prot;
	int vmm_flags;
	struct page_snapshot* next;
};

/**
 * @brief Create a snapshot of pages
 *
 * @param mm_struct The mm struct
 * @param virtual The virtual address to start at
 * @param size The size to snapshot
 *
 * @return A pointer to the snapshots
 */
__attribute__((deprecated))
struct page_snapshot* snapshot_pages(struct mm* mm_struct, uintptr_t virtual, size_t size);

/**
 * @brief Restore pages and VMA's from a snapshot
 *
 * @param mm_struct The mm struct
 * @param snapshots The snapshots to restore from
 */
__attribute__((deprecated))
void snapshot_restore_pages(struct mm* mm_struct, struct page_snapshot* snapshots);

/**
 * @brief Clean up snapshots
 *
 * @param snapshots The snapshots
 * @param free Whether or not to free the pages previously allocated from the actual mappings (only if VMM_ALLOC is in the flags)
 */
__attribute__((deprecated))
void snapshot_cleanup(struct page_snapshot* snapshots, bool free);

#define TLB_BATCH_PAGE_COUNT 16

struct tlb_batch {
	pte_t* pagetable;
	uintptr_t start;
	uintptr_t end;
	size_t page_count;
	struct page* pages[TLB_BATCH_PAGE_COUNT];
};

void tlb_batch_init(struct tlb_batch* batch, pte_t* pagetable);
void tlb_batch_flush(struct tlb_batch* batch);
void tlb_batch_add_range(struct tlb_batch* batch, uintptr_t virtual);
void tlb_batch_add_page(struct tlb_batch* batch, struct page* page);

void tlb_invalidate(uintptr_t address, size_t size);
