#pragma once

#include <crescent/asm/errno.h>
#include <crescent/core/locking.h>
#include <crescent/mm/vmm.h>

struct mm;

struct vma {
	uintptr_t start, top;
	mmuflags_t prot;
	int flags;
	struct vma* prev, *next;
};

/**
 * @brief Find a virtual memory area based on an address
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to grab it from
 * @param address The address to look up
 */
struct vma* vma_find(struct mm* mm, void* address);

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
 * @return -errno on failure
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
 * @return -errno on failure
 */
int vma_protect(struct mm* mm, void* address, size_t size, mmuflags_t prot);

/**
 * @brief Remove a VMA from the list
 *
 * LOCKING: Does not grab any locks, you are expected to take mm->vma_list_lock
 *
 * @param mm The mm struct to apply it to
 * @param addr The address to 
 */
int vma_unmap(struct mm* mm, void* addr, size_t size);
