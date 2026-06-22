#pragma once

#include <lunar/types.h>
#include <lunar/list.h>
#include <lunar/mutex.h>
#include <arch/page.h>

struct vma;

struct mm {
	pte_t* pagetable;
	struct list_head vma_list; /* struct vma */
	uintptr_t mmap_start, mmap_end;
	mutex_t mutex;
};

/**
 * @brief Get a physical address using HHDM
 * @param virtual The virtual address
 * @return The physical address
 */
physaddr_t hhdm_physical(const void* virtual);

/**
 * @brief Get a virtual address using HHDM
 * @param physical The physical address
 * @return The pointer to the memory
 */
void* hhdm_virtual(physaddr_t physical);

/**
 * @brief Get the base of HHDM
 * @return A pointer to the beginning of HHDM
 */
void* hhdm_base(void);

typedef enum {
	MM_ZONE_DMA = (1 << 0), /* Memory less than 16MiB */
	MM_ZONE_DMA32 = (1 << 1), /* Memory less than 4GiB */
	MM_ZONE_NORMAL = (1 << 2), /* Memory above 4GiB */
	MM_NOFAIL = (1 << 3), /* Allocation will never fail */
	MM_ATOMIC = (1 << 4) /* Allocation cannot sleep */
} mm_t;

#define MAX_ORDER 11

static inline unsigned int get_order(size_t size) {
	if (size <= PAGE_SIZE)
		return 0;
	unsigned long pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	return (sizeof(unsigned long) * 8 - __builtin_clzl(pages - 1));
}

/**
 * @brief Get memory usage statistics
 * @param[out] total_page_count Number of total pages on the system
 * @param[out] free_page_count Number of free pages
 */
void mm_get_free_pages(size_t* total_page_count, size_t* free_page_count);

/**
 * @brief Allocate physical pages
 * @param mm_flags The flags for how the allocation should be done
 * @param order 2 ^ order pages to allocate (0 for one page, 1 for 2 pages, 2 for 4 pages)
 * @return The physical address of the page. Returns 0 for no memory.
 */
physaddr_t alloc_pages(mm_t mm_flags, unsigned int order);

/**
 * @brief Free physical pages
 * @param addr The address of the page(s)
 * @param order The 2 ^ order of pages to free (usually the same as whatever you gave alloc_pages)
 */
void free_pages(physaddr_t addr, unsigned int order);

/**
 * @brief Allocate a physical page
 * @param mm_flags The flags for how the allocation should be done
 * @return The physical address of the page
 */
static inline physaddr_t alloc_page(mm_t mm_flags) {
	return alloc_pages(mm_flags, 0);
}

/**
 * @brief Free a physical page
 * @param addr The page to free
 */
static inline void free_page(physaddr_t addr) {
	free_pages(addr, 0);
}

void out_of_memory(void);
