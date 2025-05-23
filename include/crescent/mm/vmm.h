#pragma once

#include <crescent/asm/errno.h>
#include <crescent/core/locking.h>
#include <crescent/mm/mm.h>

enum mmu_flags {
	MMU_READ = (1 << 0),
	MMU_WRITE = (1 << 1),
	MMU_USER = (1 << 2),
	MMU_WRITETHROUGH = (1 << 3),
	MMU_CACHE_DISABLE = (1 << 4),
	MMU_EXEC = (1 << 6)
};

static inline void tlb_flush_single(void* virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

typedef unsigned long pte_t;

struct vmm_ctx {
	pte_t* pagetable;
};

void* kmap(mm_t mm_flags, size_t size, unsigned long mmu_flags);
int kprotect(void* virtual, size_t size, unsigned long mmu_flags);
int kunmap(void* virtual, size_t size);
void vmm_init(void);
