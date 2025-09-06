#pragma once

#include <crescent/asm/errno.h>
#include <crescent/core/spinlock.h>
#include <crescent/mm/vmm.h>
#include <crescent/lib/list.h>

struct mm;

struct vma {
	uintptr_t start, top;
	mmuflags_t prot;
	int flags;
	struct list_node link;
};

/**
 * @brief Find a virtual memory area based on an address
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to grab it from
 * @param address The address to look up
 *
 * @return The address of the VMA struct, NULL if not found
 */
struct vma* vma_find(struct mm* mm, const void* address);

/**
 * @brief Add a virtual memory area to the VMA list
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to apply it to
 * @param hint A hint on where the mapping should be placed, rounded to the nearest page boundary
 * @param size The size of the mapping, rounded up to the nearest page boundary
 * @param prot Protection flags for the page
 * @param flags Unimplemented
 * @param ret Where the address will be stored, uninitialized on failure
 *
 * @retval 0 Successful
 * @retval -EINVAL Size is either 0 OR there is a fixed mapping AND hint is NULL OR misaligned
 * @retval -ERANGE Integer overflow when rounding or adding
 * @retval -EEXIST VMA already exists AND no fixed/replace mapping
 * @retval -ENOMEM Ran out of virtual memory space
 */
int vma_map(struct mm* mm, void* hint, size_t size, mmuflags_t prot, int flags, void** ret);

/**
 * @brief Change the protection flags for a VMA
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to apply it to
 * @param address The address to change
 * @param size The size of the mapping to change, rounded up to the nearest page boundary
 * @param prot The new protection flags
 *
 * @retval 0 Successful
 * @retval -EINVAL Size is 0 or address is NULL or address is misaligned
 * @retval -ERANGE Integer overflow when adding or rounding
 * @retval -ENOENT VMA not found
 */
int vma_protect(struct mm* mm, void* address, size_t size, mmuflags_t prot);

/**
 * @brief Remove a VMA from the list
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to apply it to
 * @param addr The address to remove
 * @param size The size to remove
 *
 * @retval 0 Success
 * @retval -EINVAL Address is misaligned
 * @retval -ERANGE Integer overflow when adding or rounding
 * @retval -ENOENT VMA not found
 */
int vma_unmap(struct mm* mm, void* addr, size_t size);
