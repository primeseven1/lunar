#pragma once

#include <crescent/compiler.h>
#include <crescent/core/locking.h>

#define PAGE_SIZE 0x1000ul
#define HUGEPAGE_2M_SIZE 0x200000ul
#define PAGE_SHIFT 12
#define HUGEPAGE_2M_SHIFT 21

typedef enum {
	MMU_READ = (1 << 0),
	MMU_WRITE = (1 << 1),
	MMU_USER = (1 << 2),
	MMU_WRITETHROUGH = (1 << 3),
	MMU_CACHE_DISABLE = (1 << 4),
	MMU_EXEC = (1 << 5)
} mmuflags_t;

enum vmm_flags {
	VMM_ALLOC = (1 << 0),
	VMM_PHYSICAL = (1 << 1),
	VMM_FIXED = (1 << 2),
	VMM_NOREPLACE = (1 << 3),
	VMM_IOMEM = (1 << 4)
};

static inline void tlb_flush_single(void* virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

static inline void tlb_flush_range(void* virtual, size_t size) {
	unsigned long count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (unsigned long i = 0; i < count; i++)
		tlb_flush_single((u8*)virtual + (PAGE_SIZE * i));
}

typedef unsigned long pte_t;
struct mm;

/**
 * @brief Map some memory to the kernel address space
 *
 * If VMM_ALLOC is used, this function will allocate non-contiguous physical pages
 * to map the memory. This function takes an optional argument that can be NULL, if it's not
 * NULL, it will assume the memory pointed to it will be a type of mm_t.
 *
 * If VMM_PHYSICAL is used, this function will map the virtual address to a physical address,
 * optional must not be NULL, otherwise this function will return NULL. The optional argument
 * will be assumed to be pointing to a type of physaddr_t.
 *
 * If VMM_IOMEM is used, this function will use the I/O memory VMA. When this flag is used,
 * VMAP_PHYSICAL is implied. Avoid use of this flag directly, use iomap/iounmap instead.
 *
 * If VMM_FIXED is used, it places the mapping at that exact address, replacing any other mappings
 * at that addresss, unless the VMM_NOREPLACE flag is used.
 *
 * @param hint Hint for where to place the mapping. Does not need to be 
 * @param size The size of the mapping
 * @param mmu_flags The MMU flags to use for the pages
 * @param flags The vmm flags to use
 * @param optional Optional argument depending on the flags
 *
 * @return The pointer to the block
 */
void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional);

/**
 * @brief Change the MMU flags for a virtual address
 *
 * @param virtual The virtual address to change, must be aligned
 * @param size The size of the mapping you want to change
 * @param mmu_flags The new MMU flags to use
 * @param flags The vmm flags to use
 *
 * @return -errno on failure
 */
int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags);

/**
 * @brief Unmap a block allocated with vmap
 *
 * @param virtual The virtual address, must be aligned
 * @param size The original size of the mapping
 * @param flags The vmm flags to use
 *
 * @return -errno on failure
 */
int vunmap(void* virtual, size_t size, int flags);

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

void vmm_switch_mm_struct(struct mm* new_ctx);
