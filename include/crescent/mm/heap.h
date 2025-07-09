#pragma once

#include <crescent/common.h>
#include <crescent/mm/mm.h>

/**
 * @brief Allocate memory from a heap pool
 *
 * Unless the pointer returned is NULL, the memory is guarunteed to
 * be zeroed.
 *
 * @param size The size of the allocation
 * @param mm_flags The conditions for the allocation
 *
 * @return A pointer to the block of memory
 */
void* kmalloc(size_t size, mm_t mm_flags);

/**
 * @brief Allocate memory from a heap pool and zero the memory
 *
 * @param size The size of the allocation
 * @param mm_flags The conditions for the allocation
 *
 * @param A pointer to the memory
 */
static inline void* kzalloc(size_t size, mm_t mm_flags) {
	void* ret = kmalloc(size, mm_flags);
	if (!ret)
		return NULL;
	__builtin_memset(ret, 0, size);
	return ret;
}

/**
 * @brief Free a block of memory from a heap pool
 * @param ptr The pointer to the block of memory
 */
void kfree(void* ptr);

/**
 * @brief Reallocate a block of memory
 *
 * The memory from the old block will be copied over to the new block.
 * If the new size is less than the old size, the memory up to new size will
 * be copied. If the new size is more than the old size, the memory after
 * old size is guarunteed to be zeroed.
 *
 * @param old The old pointer
 * @param new_size The new size of the block
 * @param mm_flags The conditions for the allocation
 *
 * @return A new pointer to the new block
 */
void* krealloc(void* old, size_t new_size, mm_t mm_flags);

void heap_init(void);
