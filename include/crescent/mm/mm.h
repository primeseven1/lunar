#pragma once

#include <crescent/types.h>
#include <crescent/mm/vma.h>
#include <crescent/mm/vmm.h>

struct mm {
	pte_t* pagetable;
	struct vma* vma_list;
	spinlock_t vma_list_lock;
	void* mmap_start, *mmap_end;
};

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
