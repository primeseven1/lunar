#pragma once

#include <crescent/compiler.h>
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

/**
 * @brief Map I/O memory to the virtual address space
 *
 * @param physical The physical address to map
 * @param size The size of the mapping
 * @param mmu_flags The protection flags
 *
 * @return NULL on failure, otherwise you get an address in the iomem vma
 */
void __iomem* iomap(physaddr_t physical, size_t size, unsigned long mmu_flags);

/**
 * @brief Unmap I/O memory
 *
 * @param virtual The virtual address to unmap
 * @param size The size of the mapping
 *
 * @return 0 on success, -errno on failure
 */
int iounmap(void __iomem* virtual, size_t size);

/**
 * @brief Map some memory into the virtual address space
 *
 * @param mm_flags The MM flags for the memory allocations
 * @param size The size of the mapping
 * @param mmu_flags The protection flags to use for the mapping
 *
 * @return NULL on failure, otherwise you get an address in the kernel vma
 */
void* kmap(mm_t mm_flags, size_t size, unsigned long mmu_flags);

/**
 * @brief Change the MMU flags of a page(s)
 *
 * @param virtual The virtual address
 * @param size The size of the mapping
 * @param mmu_flags The new MMU flags
 *
 * @return 0 on success, -errno on failure
 */
int kprotect(void* virtual, size_t size, unsigned long mmu_flags);

/**
 * @brief Unmap virtual memory pages
 *
 * @param virtual The virtual address to unmap
 * @param size The size of the mapping
 *
 * @return 0 on success, -errno on failure
 */
int kunmap(void* virtual, size_t size);

void vmm_init(void);
