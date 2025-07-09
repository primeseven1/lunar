#pragma once

#include <crescent/compiler.h>
#include <crescent/asm/errno.h>
#include <crescent/core/locking.h>
#include <crescent/mm/mm.h>

typedef enum {
	MMU_READ = (1 << 0),
	MMU_WRITE = (1 << 1),
	MMU_USER = (1 << 2),
	MMU_WRITETHROUGH = (1 << 3),
	MMU_CACHE_DISABLE = (1 << 4),
	MMU_EXEC = (1 << 6)
} mmuflags_t;

enum vmap_flags {
	VMAP_ALLOC = (1 << 0),
	VMAP_FREE = (1 << 1),
	VMAP_PHYSICAL = (1 << 2),
	VMAP_HUGEPAGE = (1 << 3),
	VMAP_IOMEM = (1 << 4)
};

static inline void tlb_flush_single(void* virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

typedef unsigned long pte_t;

struct vmm_ctx {
	pte_t* pagetable;
};

/**
 * @brief Map some memory to the kernel address space
 *
 * If VMAP_ALLOC is used, this function will allocate non-contiguous physical pages
 * to map the memory. This function takes an optional argument that can be NULL, if it's not
 * NULL, it will assume the memory pointed to it will be a type of mm_t.
 *
 * If VMAP_PHYSICAL is used, this function will map the virtual address to a physical address,
 * optional must not be NULL, otherwise this function will return NULL. The optional argument
 * will be assumed to be pointing to a type of physaddr_t.
 *
 * If VMAP_HUGEPAGE is used, this function will use 2MiB hugepages instead of 4K pages.
 *
 * If VMAP_IOMEM is used, this function will use the I/O memory VMA. When this flag is used,
 * VMAP_PHYSICAL is implied. Don't use this flag directly, use iomap/iounmap instead.
 *
 * @param hint Ignored for now
 * @param size The size of the mapping
 * @param flags The vmap flags to use
 * @param mmu_flags The MMU flags to use for the pages
 * @param optional Optional argument depending on the flags
 *
 * @return The pointer to the block
 */
void* vmap(void* hint, size_t size, unsigned int flags, mmuflags_t mmu_flags, void* optional);

/**
 * @brief Change the MMU flags for a virtual address
 *
 * Use the VMAP_HUGEPAGE flag if the pages are hugepages (obviously). Not doing
 * so will cause this function to return -EFAULT.
 *
 * @param virtual The virtual address to change
 * @param size The size of the mapping you want to change
 * @param flags The vmap flags to use
 * @param mmu_flags The new MMU flags to use
 *
 * @return -errno on failure
 */
int vprotect(void* virtual, size_t size, unsigned int flags, mmuflags_t mmu_flags);

/**
 * @brief Unmap a block allocated with vmap
 *
 * Use VMAP_FREE if you want to free the physical pages associated with this mapping.
 *
 * Use VMAP_HUGEPAGE If the pages to unmap are hugepages.
 *
 * @param virtual The virtual address
 * @param size The original size of the mapping
 * @param flags The vmap flags to use
 *
 * @return -errno on failure
 */
int vunmap(void* virtual, size_t size, unsigned int flags);

/**
 * @brief Map pages as IO memory
 *
 * If the caching mode isn't writethrough, the cache is disabled on the page
 *
 * @param physical The address of the IO memory
 * @param size The size of the mapping, the page offset is automatically added
 * @param mmu_flags The MMU flags to use
 *
 * @return The pointer to the memory, the page offset is automatically added
 */
void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags);

/**
 * @brief Unmap memory from IO space
 *
 * @param virtual The virtual address, the page offset is automatically subtracted
 * @param size The size of the mapping, the page offset is automatically added
 *
 * @return -errno on failure
 */
int iounmap(void __iomem* virtual, size_t size);

void vmm_init(void);
