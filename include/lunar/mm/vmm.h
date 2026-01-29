#pragma once

#include <lunar/compiler.h>
#include <lunar/core/spinlock.h>
#include <lunar/mm/mm.h>

#define KSTACK_SIZE 0x4000

struct mm;

enum vmm_flags {
	VMM_ALLOC = (1 << 0), /* Allocate physical memory for the mapping */
	VMM_PHYSICAL = (1 << 1), /* Map directly to a physical address */
	VMM_FIXED = (1 << 2), /* Place the mapping exactly at the hint, regardless of what's already there */
	VMM_NOREPLACE = (1 << 3), /* Prevents a fixed mapping from replacing anything, use with VMM_FIXED */
	VMM_IOMEM = (1 << 4), /* Do not use directly, use iomap()/iounmap() */
	VMM_HUGEPAGE_2M = (1 << 5), /* Mapping should use 2MiB pages */
	VMM_USER = (1 << 6) /* Do not use directly, use usermap()/userprotect()/userunmap() */
};

struct vmm_usermap_info {
	struct mm* mm_struct;
};

/**
 * @brief Map memory into kernel space
 *
 * @param hint A hint on where to place the mapping
 * @param size The size of the mapping
 * @param mmu_flags Page protection flags
 * @param flags VMM specific flags
 * @param optional Extra argument based on flags
 *
 * @retval -EINVAL Invalid flags, invalid hint, size is zero, or optional is NULL but required
 * @retval -ENOMEM Out of memory
 * @retval -EEXIST Mapping already exists AND flags have VMM_FIXED | VMM_NOREPLACE
 * @retval -ERANGE hint + size overflows
 * @return The pointer to the memory
 */
void* vmap(void* hint, size_t size, mmuflags_t mmu_flags, int flags, void* optional);

/**
 * @brief Change the protection flags for a mapping in kernel space
 *
 * @param virtual The page(s)
 * @param size The size of the mapping
 * @param mmu_flags The new protection flags
 * @param flags Unimplemented
 * @param optional Unimplemented
 *
 * @retval -EINVAL Virtual address isn't aligned or size is zero
 * @retval -ENOENT Mapping does not exist
 * @retval -ERANGE virtual + size overflows
 * @retval 0 Successful
 */
int vprotect(void* virtual, size_t size, mmuflags_t mmu_flags, int flags, void* optional);

/**
 * @brief Unmap kernel memory
 *
 * @param virtual The virtual address
 * @param size The size of the mapping
 * @param flags Unimplemented
 * @param optional Unimplemented
 *
 * @retval -EINVAL Virtual address isn't aligned or size is zero
 * @retval -ENOENT Mapping does not exist
 * @retval -ERANGE virtual + size overflow
 * @retval 0 Successful
 */
int vunmap(void* virtual, size_t size, int flags, void* optional);

/**
 * @brief Map I/O memory into kernel space
 *
 * @param physical The physical address
 * @param size The size of the mapping
 * @param mmu_flags Protection flags
 *
 * @return The pointer to the memory, NULL on failure
 */
void __iomem* iomap(physaddr_t physical, size_t size, mmuflags_t mmu_flags);

/**
 * @brief Unmap I/O memory
 *
 * @param virtual The virtual address to unmap
 * @param size The size of the mapping
 *
 * @retval -EINVAL The size is zero
 * @retval -ENOENT Virtual address not mapped
 * @retval 0 Successful
 */
int iounmap(void __iomem* virtual, size_t size);

/**
 * @brief Map memory into user space
 *
 * @param hint A hint on where to place the mapping
 * @param size The size of the mapping
 * @param mmu_flags Page protection flags
 * @param flags VMM specific flags
 * @param usermap_info Information on how to map the memory in user space
 *
 * @retval -EINVAL Invalid flags, invalid hint, size is zero, or optional is NULL but required
 * @retval -ENOMEM Out of memory
 * @retval -EEXIST Mapping already exists AND flags have VMM_FIXED | VMM_NOREPLACE
 * @retval -ERANGE hint + size overflows
 * @return The pointer to the memory
 */
void __user* usermap(void __user* hint, size_t size, mmuflags_t mmu_flags, int flags, struct vmm_usermap_info* usermap_info);

/**
 * @brief Change the protection flags for a mapping in user space
 *
 * @param virtual The page(s)
 * @param size The size of the mapping
 * @param mmu_flags The new protection flags
 * @param flags Unimplemented
 * @param usermap_info Information on how to map the memory in user space
 *
 * @retval -EINVAL Virtual address isn't aligned or size is zero
 * @retval -ENOENT Mapping does not exist
 * @retval -ERANGE virtual + size overflows
 * @retval 0 Successful
 */
int userprotect(void __user* virtual, size_t size, mmuflags_t mmu_flags, int flags, struct vmm_usermap_info* usermap_info);

/**
 * @brief Unmap user memory
 *
 * @param virtual The virtual address
 * @param size The size of the mapping
 * @param flags Unimplemented
 * @param usermap_info Information on how to unmap the memory in user space
 *
 * @retval -EINVAL Virtual address isn't aligned or size is zero
 * @retval -ENOENT Mapping does not exist
 * @retval -ERANGE virtual + size overflow
 * @retval 0 Successful
 */
int userunmap(void __user* virtual, size_t size, int flags, struct vmm_usermap_info* usermap_info);

void* vmap_stack(size_t size, bool return_top);
int vunmap_stack(void* stack, size_t size, bool is_top);
void __user* uvmap_stack(size_t size, bool return_top, struct mm* mm);
int uvunmap_stack(void __user* stack, size_t size, bool is_top, struct mm* mm);

void vmm_tlb_init(void);
void vmm_cpu_init(void);
void vmm_init(void);
