#pragma once

#include <crescent/core/locking.h>

struct vma {
	void* start, *end; /* Not modified after creation */
	u8* free_list; /* Free list bitmap */
	unsigned long page_count; /* The number of pages currectly allocated to the vma */
	spinlock_t lock;
};

/**
 * @brief Allocate pages from a VMA
 *
 * If `align` is not a power of two and not zero, NULL is returned.
 * If `align is` zero, then the address will be aligned by PAGE_SIZE.
 *
 * Pages are guarunteed to be aligned by 4K.
 *
 * LOCKING:
 * vma->lock is expected to be unlocked before calling this function
 *
 * @param vma The VMA to allocate from
 * @param page_count The number of pages to allocate
 * @param align The alignment for the pages
 *
 * @return The address of the page
 */
void* vma_alloc_pages(struct vma* vma, unsigned long page_count, size_t align);

/**
 * @brief Free pages from a VMA
 *
 * LOCKING:
 * vma->lock is expected to be unlocked before calling this function.
 *
 * @param vma The VMA to free pages from
 * @param addr The address to free
 * @param page_count The number of pages to free
 */
void vma_free_pages(struct vma* vma, void* addr, unsigned long page_count);

/**
 * @brief Create a VMA
 *
 * The VMA cannot be expanded past the end of the VMA
 *
 * @param start The start of the VMA
 * @param end The end of the VMA.
 * @param page_count The initial page count
 *
 * @return The pointer to the newly created VMA
 */
struct vma* vma_create(void* start, void* end, unsigned long page_count);

int vma_split(struct vma* vma, void* split_point, struct vma** ret);
int vma_merge(struct vma* dvma, struct vma* svma);

/**
 * @brief Destroy a VMA
 * @param vma The VMA to destroy
 *
 * @retval 0 Success
 * @retval -EEXIST There are still pages that are in use from the VMA
 */
int vma_destroy(struct vma* vma);
