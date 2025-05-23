#pragma once

#include <crescent/mm/mm.h>

#define MAX_ORDER 12

physaddr_t alloc_pages(mm_t mm_flags, unsigned int order);
void free_pages(physaddr_t addr, unsigned int order);

static inline physaddr_t alloc_page(mm_t mm_flags) {
	return alloc_pages(mm_flags, 0);
}

static inline void free_page(physaddr_t addr) {
	free_pages(addr, 0);
}

void buddy_init(void);
