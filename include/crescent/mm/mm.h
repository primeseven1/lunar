#pragma once

#include <crescent/types.h>

#define PAGE_SIZE 0x1000ul
#define HUGEPAGE_SIZE 0x200000ul
#define PAGE_SHIFT 12

static inline unsigned int get_order(size_t size) {
	if (size <= PAGE_SIZE)
		return 0;
	unsigned long pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	return (sizeof(unsigned long) * 8 - __builtin_clzl(pages - 1));
}

typedef enum {
	MM_ZONE_DMA = (1 << 0),
	MM_ZONE_DMA32 = (1 << 1),
	MM_ZONE_NORMAL = (1 << 2),
} mm_t;
