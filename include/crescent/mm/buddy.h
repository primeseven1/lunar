#pragma once

#include <crescent/mm/mm.h>

#define MAX_ORDER 12

/**
 * @brief Allocate physical pages
 *
 * @param mm_flags The conditions for the allocation
 * @param order The 2 ^ order pages to allocate
 *
 * @return The physical address of the page, 0 on failure
 */
physaddr_t alloc_pages(mm_t mm_flags, unsigned int order);

/**
 * @brief Free physical pages
 *
 * @param addr The address to free
 * @param order The 2 ^ order pages to free
 */
void free_pages(physaddr_t addr, unsigned int order);

/**
 * @brief Allocate a single page
 *
 * Wrapper around alloc_pages
 *
 * @param mm_flags The conditions for the allocation
 * @return The physical address of the page
 */
static inline physaddr_t alloc_page(mm_t mm_flags) {
	return alloc_pages(mm_flags, 0);
}

/**
 * @brief Free a single page
 *
 * Wrapper around free_pages
 *
 * @param addr The address to free
 */
static inline void free_page(physaddr_t addr) {
	free_pages(addr, 0);
}

void buddy_init(void);
