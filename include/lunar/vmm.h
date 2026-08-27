#pragma once

#include <lunar/mm.h>
#include <lunar/compiler.h>

#define VMM_ALLOC (1 << 0) /* Allocate pages automatically for the mapping, do NOT use if you need contiguous memory */
#define VMM_FIXED (1 << 1) /* Address hints are no longer hints */
#define VMM_NOREPLACE (1 << 2) /* Do not replace mappings when using VMM_FIXED, instead return -EEXIST when this occurs */
#define VMM_HUGETLB (1 << 3) /* Use hugepages */
#define VMM_HUGETLB_2MB (0b01 << 4)
#define VMM_HUGETLB_1GB (0b10 << 4)
#define VMM_SEALED (1 << 6) /* Do not allow changes to a mapping, throughout the life of the program */
#define VMM_STACK (1 << 7) /* The mapping is for a stack */
#define VMM_IOMEM (1 << 8) /* The mapping is for MMIO */

#define VMM_HUGETLB_SIZE_MASK (VMM_HUGETLB_2MB | VMM_HUGETLB_1GB)
#define VMM_ALL_MASK (VMM_ALLOC | VMM_FIXED | VMM_NOREPLACE | VMM_HUGETLB | VMM_HUGETLB_SIZE_MASK | VMM_SEALED | VMM_STACK | VMM_IOMEM)

#define VMM_HUGETLB_2MB_SIZE 0x200000
#define VMM_HUGETLB_1GB_SIZE 0x40000000

/**
 * @brief Get the CPU's MM struct
 * @return The pointer to the MM struct
 */
struct mm* current_mm(void);

/**
 * @brief Called during page table teardown when destroying a page table
 *
 * Whenever the page table walker encounters a leaf mapping, the function calls this function to
 * release the page.
 *
 * @param address The address to tear down
 */
void vm_pagetable_teardown_leaf(physaddr_t address);

/**
 * @brief Map pages into kernel space
 *
 * If a page is NULL in the page array, it becomes a guard page with no permisions.
 *
 * @param hint The hint on where to place the mapping
 * @param pages The page array to map
 * @param page_count Number of pages in the page array
 * @param prot Protection flags
 * @param flags VMM flags
 *
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL Invalid combination of arguments
 * @retval -ENOSYS Combination of arguments recognized but not supported right now
 * @retval -ENOTSUP Combination of arguments explicitly not supported (eg: 1GB hugepages)
 * @retval -ERANGE The page count is too large to represent the size
 * @return A pointer to the mapped memory, or -errno on failure
 */
void* vm_map(void* hint, struct page** pages, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Map a physical address range into kernel space
 *
 * @param hint The hint on where to place the mapping
 * @param physical The physical address
 * @param page_count The number of pages to map
 * @param prot Page protection flags
 * @param flags VMM flags
 *
 * @retval -EACCES Page has zero references AND is physical RAM
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL Invalid combination of arguments
 * @retval -ENOSYS Combination of arguments recognized but not supported right now
 * @retval -ENOTSUP Combination of arguments explicitly not supported (eg: 1GB hugepages)
 * @retval -ERANGE The page count is too large to represent the size
 * @return A pointer to the memory, or -errno on failure
 */
void* vm_map_physical(void* hint, physaddr_t physical, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Change the MMU permissions on pages
 *
 * @param virtual The virtual address
 * @param page_count The number of pages to change
 * @param prot Page protection flags
 * @param flags Unused right now, use 0
 *
 * @retval -ENOMEM Out of memory (can happen when failing to split a VMA)
 * @retval -EINVAL The virtual address is not page aligned, or NULL
 * @retval -ENOENT No mappings in range, or only partially in range
 * @retval -ERANGE The page count is too large to represent the size
 * @return 0 on success, or -errno on failure
 */
int vm_protect(void* virtual, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Unmap virtual pages
 *
 * @param virtual The virtual address to unmap
 * @param page_count The number of pages to unmap
 * @param flags VMM flags
 *
 * @retval -ENOMEM Out of memory (can happen when failing to split a VMA)
 * @retval -EINVAL The virtual address is not page aligned, or NULL
 * @retval -ENOENT No mapped addresses in range
 * @retval -ERANGE The page count is too large to represent the size
 * @return 0 on success, -errno on failure
 */
int vm_unmap(void* virtual, size_t page_count, int flags);

/**
 * @brief Unmap virtual pages
 *
 * Unlike vm_unmap, this function will handle the -ENOMEM error. Because this function doesn't
 * return any error, this function will panic when failing to unmap the memory.
 *
 * @param virtual The virtual address to unmap
 * @param page_count The number of pages to unmap
 * @param flags VMM flags
 */
void vm_unmap_force(void* virtual, size_t page_count, int flags);

/**
 * @brief Map pages into user space
 *
 * @param hint Hint on where to place the mapping
 * @param pages The pages to map
 * @param page_count Number of pages in the array
 * @param prot Page protection flags
 * @param flags VMM_* flags
 *
 * @retval -ESRCH Not in a user context
 * @retval -ENOMEM Out of memory
 * @retval -EINVAL A combination of arguments was rejected
 * @retval -ENOSYS A combination of arguments/flags are recognized but not supported right now
 * @retval -ENOTSUP A combination of arguments/flags are explicitly not supported (eg: 1GB hugepages)
 * @retval -ERANGE The page count is too large to represent the size
 * @return -errno on failure, or a pointer to the memory
 */
void __user* vm_map_user(void __user* hint, struct page** pages, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Protect user pages
 *
 * @param virtual The virtual address to protect
 * @param pages The number of pages
 * @param prot Page protection flags
 * @param flags VMM_* flags
 *
 * @retval -ESRCH Not in a user context
 * @retval -ENOMEM Out of memory (Can happen when splitting a VMA)
 * @retval -ENOENT No mappings in range, or only partially in range
 * @retval -ERANGE The page count is too large to represent the size
 * @return -errno on failure, 0 on success
 */
int vm_protect_user(void __user* virtual, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Unmap user pages
 *
 * @param virtual The virtual address to unmap
 * @param page_count Number of pages to unmap
 * @param flags VMM_* flags
 *
 * @retval -ESRCH Not in a user context
 * @retval -ENOMEM Out of memory (Can happen when splitting a VMA)
 * @retval -ENOENT No mappings in range
 * @retval -ERANGE The page count is too large to represent the size
 * @return -errno on failure, 0 on success
 */
int vm_unmap_user(void __user* virtual, size_t page_count, int flags);

/**
 * @brief Map I/O memory
 *
 * @param physical The physical address, can be misaligned
 * @param size Size of the mapping
 * @param cache Caching mode for the pages
 *
 * @return A pointer to the memory including the alignment, or -errno on failure. Read vm_map_physical for information on errors.
 */
void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t cache);

/**
 * @brief Unmap I/O memory
 *
 * @param virtual The virtual address
 * @param size The size to unmap
 *
 * @return 0 on success, -errno on failure
 */
int iounmap(void __iomem* virtual, size_t size);

/**
 * @brief Allocate memory using the VMM
 *
 * This function allocates virtually contiguous memory. A guard page is placed at the end.
 *
 * @param size The size to allocate
 * @return A pointer to the memory
 */
void* vmalloc(size_t size);

/**
 * @brief Re-allocate memory allocated by vmalloc()
 *
 * @param ptr The original pointer
 * @param size The new size
 *
 * @return A pointer to the new block of memory
 */
void* vrealloc(void* ptr, size_t size);

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
