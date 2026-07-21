#pragma once

#include <lunar/mm.h>
#include <lunar/compiler.h>

#define VMM_ALLOC (1 << 0) /* Allocate physical memory for the pages (optional argument NULL)*/
#define VMM_PHYSICAL (1 << 1) /* Map pages to a physical address (optional pointing to a physaddr_t*) */
#define VMM_FIXED (1 << 2) /* Only place the mapping at the exact hint */
#define VMM_NOREPLACE (1 << 3) /* Used with VMM_FIXED, but will not replace mappings that already exist, instead returning -EEXIST */
#define VMM_IOMEM (1 << 4) /* Mapping is used for MMIO. Do not use directly, use iomap() instead */
#define VMM_HUGETLB (1 << 5) /* Use a page size bigger than the default. Uses 2MiB pages when page size is not specified */
#define VMM_HUGETLB_2MB (1 << 6) /* Use a 2MiB page, use in conjunction with VMM_HUGETLB */
#define VMM_HUGETLB_1GB (1 << 7) /* Unsupported, returns -ENOTSUP when used */
#define VMM_USER (1 << 8) /* Mapping is for user space. Do not use directly, use usermap() instead */
#define VMM_SEALED (1 << 9) /* Mapping cannot be changed even when VMM_FIXED is used, returning -EPERM when this happens */
#define VMM_STACK (1 << 10) /* Mapping is for a stack */

struct vmm_usermap_info {
	struct mm* mm_struct;
};

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

__attribute__((deprecated("Use vm_map()")))
void* vmap(void* hint, size_t size, pgprot_t prot, int flags, void* optional);
__attribute__((deprecated("Use vm_protect()")))
int vprotect(void* virtual, size_t size, pgprot_t prot, int flags, void* optional);
__attribute__((deprecated("Use vm_unmap()")))
int vunmap(void* virtual, size_t size, int flags, void* optional);
__attribute__((deprecated))
void __iomem* iomap(physaddr_t physical, size_t size, pgprot_t cache);
__attribute__((deprecated))
int iounmap(void __iomem* virtual, size_t size);
__attribute__((deprecated))
void __user* usermap(void __user* hint, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info);
__attribute__((deprecated))
int userprotect(void __user* virtual, size_t size, pgprot_t prot, int flags, struct vmm_usermap_info* usermap_info);
__attribute__((deprecated))
int userunmap(void __user* virtual, size_t size, int flags, struct vmm_usermap_info* usermap_info);

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
