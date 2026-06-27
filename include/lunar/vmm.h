#pragma once

#include <lunar/mm.h>
#include <lunar/compiler.h>

#define VMM_ALLOC (1 << 0)
#define VMM_PHYSICAL (1 << 1)
#define VMM_FIXED (1 << 2)
#define VMM_NOREPLACE (1 << 3)
#define VMM_IOMEM (1 << 4)
#define VMM_HUGETLB (1 << 5)
#define VMM_HUGETLB_2MB (1 << 6)
#define VMM_HUGETLB_1GB (1 << 7)
#define VMM_USER (1 << 8)
#define VMM_SEALED (1 << 9)
#define VMM_STACK (1 << 10)

struct vmm_usermap_info {
	struct mm* mm_struct;
};

/**
 * @brief Map kernel memory
 *
 * @param hint A hint on where the mapping should be placed
 * @param size The size of the mapping
 * @param prot Protection flags
 * @param flags Flags for how the mapping should be done
 * @param optional Optional argument depending on the flags
 *
 * @retval -ENOMEM Out of memory
 * @retval -ENOTSUP Trying to map a 1GB page
 * @retval -EINVAL Invalid flags/prot, size is zero, hint is misaligned with VMM_FIXED, or optional is required but NULL
 * @retval -EEXIST Mapping already exists (only happens when VMM_FIXED | VMM_NOREPLACE is used)
 * @retval -ERANGE hint + size overflows
 * @return A pointer to the memory, or -errno as a pointer on failure
 */
void* vmap(void* hint, size_t size, pgprot_t prot, int flags, void* optional);

/**
 * @brief Protect kernel memory
 *
 * @param virtual The memory to protect
 * @param size The size to protect
 * @param prot New protection flags
 * @param flags VMM_USER and VMM_IOMEM cause this function to return -EINVAL, everything else is ignored
 * @param optional NULL, nothing is implemented here
 *
 * @retval -EINVAL Misaligned address, size is zero, invalid prot or flags, or optional is required but is NULL
 * @retval -ENOENT Mapping does not exist
 * @retval -ERANGE virtual + size overflows
 * @retval 0 Successful
 */
int vprotect(void* virtual, size_t size, pgprot_t prot, int flags, void* optional);

/**
 * @brief Unmap kernel memory
 *
 * @param virtual The memory to unmap
 * @param size The size to unmap
 * @param flags VMM_USER and VMM_IOMEM cause this function to return -EINVAL, everything else is ignored
 * @param optional NULL, nothing is implemented here
 *
 * @retval -EINVAL Misaligned address, size is zero, invalid flags, or optional is required but is NULL
 * @retval -ENOENT Mapping does not exist
 * @retval 0 Successful
 */
int vunmap(void* virtual, size_t size, int flags, void* optional);

/**
 * @brief Map I/O memory into kernel space
 *
 * @param physical The physical address
 * @param size The size of the mapping
 * @param cache Caching mode
 *
 * @return The pointer to the memory, NULL on failure
 */
void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t cache);

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
void __user* usermap(void __user* hint, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info);

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
int userprotect(void __user* virtual, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info);

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

/**
 * @brief Allocate non-contiguous memory
 * @param size The size of the allocation
 * @return The address of the memory
 */
void* vmalloc(size_t size);

/**
 * @brief Free memory allocated with vmalloc()
 * @param ptr The pointer to free
 */
void vfree(void* ptr);

/**
 * @brief Enable TLB shootdowns
 *
 * Called only by the BSP
 */
void tlb_shootdown_init(void);
