#pragma once

#include <lunar/mm.h>
#include <lunar/compiler.h>

#define VMM_STACK (1 << 0)
#define VMM_IOMEM (1 << 1)
#define VMM_FIXED (1 << 2)
#define VMM_NOREPLACE (1 << 3)
#define VMM_HUGETLB (1 << 4)
#define VMM_HUGETLB_2MB (1 << 5)
#define VMM_HUGETLB_1GB (1 << 6)
#define VMM_SEALED (1 << 7)

/**
 * @brief Get the CPU's MM struct
 * @return The pointer to the MM struct
 */
struct mm* current_mm(void);

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
 * @return -errno on failure
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
 * @return -errno on failure
 */
void* vm_map_physical(void* hint, physaddr_t physical, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Change the MMU permissions on pages
 *
 * @param virtual The virtual address
 * @param page_count The number of pages to change
 * @param prot Page protection flags
 * @param flags VMM flags
 *
 * @return -errno on failure
 */
int vm_protect(void* virtual, size_t page_count, pgprot_t prot, int flags);

/**
 * @brief Unmap virtual pages
 *
 * @param virtual The virtual address to unmap
 * @param page_count The number of pages to unmap
 * @param flags VMM flags
 *
 * @return -errno on failure
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
 * @return -errno on failure, otherwise it returns the pointer
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
 * @return A pointer to the memory including the alignment, or -errno
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
