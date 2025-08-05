#pragma once

#include <crescent/types.h>
#include <crescent/core/locking.h>
#include <crescent/asm/errno.h>
#include <crescent/mm/mm.h>
#include <crescent/lib/list.h>

struct slab {
	void* base;
	u8* free;
	size_t in_use;
	struct list_node link;
};

struct slab_cache {
	void (*ctor)(void*);
	void (*dtor)(void*);
	struct list_head full, partial, empty;
	size_t obj_size;
	unsigned long obj_count;
	size_t align;
	mm_t mm_flags;
	spinlock_t lock;
};

/**
 * @brief Create a new slab cache
 *
 * If zero is passed to align, it will set it to 8.
 *
 * @param obj_size The size of the object. This will be rounded to the alignment
 * @param align The alignment of the object, must be a power of 2
 * @param mm_flags The MM flags for this cache
 * @param ctor Object constructor
 * @param dtor Object destructor
 *
 * @return NULL if align isn't a power of 2, or there is no memory
 */
struct slab_cache* slab_cache_create(size_t obj_size, size_t align, 
		mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*));

/**
 * @brief Destroy a slab cache
 * 
 * This function will make sure no slabs are empty before
 * attempting to destroy
 *
 * @param cache The cache to destroy
 *
 * @retval 0 Success
 * @retval -EBUSY There are still active allocations in the cache
 * @retval -EWOULDBLOCK The lock is acquired on the cache
 */
int slab_cache_destroy(struct slab_cache* cache);

/**
 * @brief Allocate memory from a slab cache
 *
 * This function will automatically try to grow the cache if all slabs are full. 
 *
 * @param cache The cache to allocate from
 * @return NULL if there is no memory available, otherwise you get a mapped virtual address
 */
void* slab_cache_alloc(struct slab_cache* cache);

/**
 * @brief Free memory from a slab cache
 *
 * If the object isn't in any slab, it will print an error message
 * and just return.
 *
 * @param obj The object to free
 */
void slab_cache_free(struct slab_cache* cache, void* obj);
