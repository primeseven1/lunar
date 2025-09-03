#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/printk.h>
#include <crescent/core/trace.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>

/* The maximum size a slab can be before the object count goes to SLAB_AFTER_CUTOFF_COUNT */
#define SLAB_SIZE_CUTOFF 512
#define SLAB_AFTER_CUTOFF_OBJ_COUNT 16

static int slab_init(struct slab_cache* cache, struct slab* slab) {
	size_t map_size = (cache->obj_count + 7) >> 3;
	size_t slab_size = cache->obj_size * cache->obj_count;

	/* Allocate free list bitmap */
	slab->free = vmap(NULL, map_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!slab->free)
		return -ENOMEM;

	/* Allocate a virtual address for the slab base */
	slab->base = vmap(NULL, slab_size, MMU_READ | MMU_WRITE, VMM_ALLOC, &cache->mm_flags);
	if (!slab->base) {
		assert(vunmap(slab->free, map_size, 0) == 0);
		return -ENOMEM;
	}

	slab->in_use = 0;
	list_node_init(&slab->link);
	return 0;
}

static int slab_cache_grow(struct slab_cache* cache) {
	struct slab* slab = vmap(NULL, sizeof(*slab), MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!slab)
		return -ENOMEM;

	int err = slab_init(cache, slab);
	if (err) {
		assert(vunmap(slab, sizeof(*slab), 0) == 0);
		return err;
	}

	list_add(&cache->empty, &slab->link);
	return 0;
}

static void* slab_take(struct slab_cache* cache, struct slab* slab) {
	size_t obj_num = SIZE_MAX;

	/* Simply find a free block within the bitmap */
	for (size_t i = 0; i < cache->obj_count; i++) {
		size_t byte_index = i >> 3;
		unsigned int bit_index = i & 7;

		if (!(slab->free[byte_index] & (1 << bit_index))) {
			slab->free[byte_index] |= (1 << bit_index);
			obj_num = i;
			break;
		}
	}

	if (obj_num == SIZE_MAX)
		return NULL;

	/* Now just get the object's pointer and call its constructor */
	void* obj = (u8*)slab->base + cache->obj_size * obj_num;
	if (cache->ctor)
		cache->ctor(obj);

	slab->in_use++;
	return obj;
}

static struct slab* __slab_find(struct list_head* slabs, unsigned long obj_count, size_t obj_size, void* obj) {
	struct slab* pos;
	list_for_each_entry(pos, slabs, link) {
		void* top = (u8*)pos->base + obj_count * obj_size;
		if (obj >= pos->base && obj < top)
			return pos;
	}

	return NULL;
}

/* Find a slab within a cache */
static struct slab* slab_find(struct slab_cache* cache, void* obj) {
	struct slab* slab = __slab_find(&cache->partial, cache->obj_count, cache->obj_size, obj);
	if (slab)
		return slab;
	slab = __slab_find(&cache->full, cache->obj_count, cache->obj_size, obj);
	if (slab)
		return slab;
	return __slab_find(&cache->empty, cache->obj_count, cache->obj_size, obj);
}

static struct slab* slab_release(struct slab_cache* cache, void* obj) {
	struct slab* slab = slab_find(cache, obj);
	if (!slab)
		return NULL; /* Let the caller do what it wants */

	/* Get the object number and then mark it as free */
	size_t obj_num = ((uintptr_t)obj - (uintptr_t)slab->base) / cache->obj_size;
	size_t byte_index = obj_num >> 3;
	size_t bit_index = obj_num & 7;

	/* Check for a double free here since the caller can't check directly */
	assert((slab->free[byte_index] & (1 << bit_index)) != 0);
	slab->free[byte_index] &= ~(1 << bit_index);

	/* Now call the destructor and return the slab the free happened on */
	if (cache->dtor)
		cache->dtor(obj);
	slab->in_use--;
	return slab;
}

void* slab_cache_alloc(struct slab_cache* cache) {
	unsigned long flags;
	spinlock_lock_irq_save(&cache->lock, &flags);

	/* Allocate from partial slabs first */
	struct list_head* list = NULL;
	if (!list_empty(&cache->partial))
		list = &cache->partial;
	else if (!list_empty(&cache->empty))
		list = &cache->empty;

	void* ret = NULL;
	/* If no slabs are available, try growing the cache */
	if (!list) {
		if (slab_cache_grow(cache) == 0)
			list = &cache->empty;
		else
			goto out;
	}

	struct slab* slab = list_first_entry(list, struct slab, link);
	ret = slab_take(cache, slab);
	assert(ret != NULL); /* Since we've already grown the cache, if this happens something bad has happened */

	/* Check to see if the slab is in the appropriate list. If not, move it. */
	if (slab->in_use == 1) {
		list_remove(&slab->link);
		list_add(&cache->partial, &slab->link);
	} else if (slab->in_use == cache->obj_count) {
		list_remove(&slab->link);
		list_add(&cache->full, &slab->link);
	}

out:
	spinlock_unlock_irq_restore(&cache->lock, &flags);
	return ret;
}

void slab_cache_free(struct slab_cache* cache, void* obj) {
	unsigned long flags;
	spinlock_lock_irq_save(&cache->lock, &flags);

	struct slab* slab = slab_release(cache, obj);
	if (!slab) {
		printk(PRINTK_ERR "mm: slab_release returned NULL, invalid object? obj: %p\n", obj);
		dump_stack();
		goto out;
	}

	/* Make sure the slab is in the appropriate list */
	if (slab->in_use == 0) {
		list_remove(&slab->link);
		list_add(&cache->empty, &slab->link);
	} else if (slab->in_use == cache->obj_count - 1) {
		list_remove(&slab->link);
		list_add(&cache->partial, &slab->link);
	}

out:
	spinlock_unlock_irq_restore(&cache->lock, &flags);
}

struct slab_cache* slab_cache_create(size_t obj_size, size_t align, 
		mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*)) {
	if (align == 0)
		align = 8;
	else if (align & (align - 1))
		return NULL;

	struct slab_cache* cache = vmap(NULL, sizeof(*cache), MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!cache)
		return NULL;

	cache->ctor = ctor;
	cache->dtor = dtor;
	list_head_init(&cache->full);
	list_head_init(&cache->partial);
	list_head_init(&cache->empty);
	cache->obj_size = ROUND_UP(obj_size, align);
	cache->obj_count = cache->obj_size < SLAB_SIZE_CUTOFF ? (PAGE_SIZE * 2) / cache->obj_size : SLAB_AFTER_CUTOFF_OBJ_COUNT;
	cache->align = align;
	cache->mm_flags = mm_flags;
	spinlock_init(&cache->lock);

	return cache;
}

int slab_cache_destroy(struct slab_cache* cache) {
	unsigned long irq;
	if (!spinlock_try_irq_save(&cache->lock, &irq))
		return -EWOULDBLOCK;
	if (!list_empty(&cache->partial) || !list_empty(&cache->full)) {
		spinlock_unlock_irq_restore(&cache->lock, &irq);
		return -EBUSY;
	}

	struct slab* slab, *tmp;
	list_for_each_entry_safe(slab, tmp, &cache->empty, link) {
		list_remove(&slab->link);
		assert(vunmap(slab->base, cache->obj_size * cache->obj_count, 0) == 0);
		assert(vunmap(slab->free, (cache->obj_count + 7) >> 3, 0) == 0);
		assert(vunmap(slab, sizeof(*slab), 0) == 0);
	}

	/* No need to release the lock here, but this also acts as a memory fence */
	spinlock_unlock_irq_restore(&cache->lock, &irq);
	assert(vunmap(cache, sizeof(*cache), 0) == 0);
	return 0;
}
