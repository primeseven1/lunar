#pragma once

#include <lunar/compiler.h>
#include <lunar/core/spinlock.h>

#define KSTACK_SIZE 0x4000

#define PAGE_SIZE 0x1000ul
#define HUGEPAGE_2M_SIZE 0x200000ul
#define PAGE_SHIFT 12
#define HUGEPAGE_2M_SHIFT 21

typedef enum {
	MMU_NONE = 0,
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
	VMM_IOMEM = (1 << 4),
	VMM_HUGEPAGE_2M = (1 << 5),
	VMM_USER = (1 << 6)
};

typedef unsigned long pte_t;

/**
 * @brief Map some memory to the kernel address space
 *
 * If VMM_ALLOC is used, this function will allocate non-contiguous physical pages
 * to map the memory. The argument optional will be ignored.
 *
 * If VMM_PHYSICAL is used, this function will map the virtual address to a physical address,
 * optional must not be NULL, otherwise this function will return -EINVAL. The optional argument
 * will be assumed to be pointing to a type of physaddr_t. The physical address must be
 * aligned, or this function will fail.
 *
 * If VMM_IOMEM is used, then VMM_PHYSICAL is implied. Do not use this flag directly, use iomap/iounmap.
 *
 * If VMM_FIXED is used, it places the mapping at that exact address, replacing any other mappings
 * at that addresss, unless the VMM_NOREPLACE flag is used.
 *
 * If VMM_USER is used, the mapping will be placed in user space.
 * If in a kthread context, -EINVAL is returned UNLESS the optional argument is provided.
 * The optional argument type is of struct mm* in this case. Using this flag in combination with VMM_PHYSICAL
 * or VMM_IOMEM is invalid and will return -EINVAL.
 *
 * If VMM_HUGEPAGE_2M is used, the mapping will use 2MiB hugepages instead of 4K pages.
 *
 * @param hint Hint for where to place the mapping. Does not need to be 
 * @param size The size of the mapping
 * @param mmu_flags The MMU flags to use for the pages
 * @param flags The vmm flags to use
 * @param optional Optional argument depending on the flags
 *
 * @return -errno on failure as a pointer, otherwise the address to the mapping is returned
 * @retval -EINVAL Bad hint (in some cases), invalid size, mmu flags, flags, or in some cases optional being NULL
 * @retval -ENOMEM Ran out of memory
 * @retval -EEXIST Mapping exists
 */
void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional);

/**
 * @brief Change the MMU flags for a virtual address
 *
 * @param virtual The virtual address to change, must be aligned
 * @param size The size of the mapping you want to change
 * @param mmu_flags The new MMU flags to use
 * @param flags Unimplemented, must be zero.
 *
 * @return -errno on failure
 */
int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags);

/**
 * @brief Unmap a block allocated with vmap
 *
 * @param virtual The virtual address, must be aligned
 * @param size The original size of the mapping
 * @param flags Unimplemented, must be zero.
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

/**
 * @brief Create a stack that is KSTACK_SIZE in length
 *
 * Adds a 4K guard page at the end of the stack.
 *
 * @return The address of the stack, the pointer returned points to the top of the stack.
 */
void* vmap_kstack(void);

/**
 * @brief Unmap a kernel stack
 * @param stack The stack to unmap
 */
int vunmap_kstack(void* stack);

void vmm_tlb_init(void);
void vmm_cpu_init(void);
void vmm_init(void);
