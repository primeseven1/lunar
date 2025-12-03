#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/mm/slab.h>
#include <lunar/mm/vmm.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/core/printk.h>
#include <lunar/core/trace.h>
#include <lunar/core/panic.h>
#include <lunar/lib/string.h>

/* The maximum size a slab can be before the object count goes to SLAB_AFTER_CUTOFF_COUNT */
#define SLAB_SIZE_CUTOFF 512
#define SLAB_AFTER_CUTOFF_OBJ_COUNT 16

static inline void* slab_alloc_struct(mm_t mm_flags, size_t size) {
	void* ptr = hhdm_virtual(alloc_pages(mm_flags, get_order(size)));
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

static inline void slab_free_struct(void* ptr, size_t size) {
	free_pages(hhdm_physical(ptr), get_order(size));
}

static int slab_init(struct slab_cache* cache, struct slab* slab) {
	size_t map_size = (cache->obj_count + 7) >> 3;
	size_t slab_size = cache->obj_size * cache->obj_count;

	/* Allocate free list bitmap */
	slab->free = slab_alloc_struct(cache->mm_flags, map_size);
	if (!slab->free)
		return -ENOMEM;

	/* Allocate a virtual address for the slab base */
	slab->base = slab_alloc_struct(cache->mm_flags, slab_size);
	if (!slab->base) {
		slab_free_struct(slab->free, map_size);
		return -ENOMEM;
	}

	slab->in_use = 0;
	list_node_init(&slab->link);
	return 0;
}

static int slab_cache_grow(struct slab_cache* cache) {
	struct slab* slab = slab_alloc_struct((cache->mm_flags & MM_ATOMIC) | MM_ZONE_NORMAL, sizeof(*slab));
	if (!slab)
		return -ENOMEM;

	int err = slab_init(cache, slab);
	if (err) {
		slab_free_struct(slab, sizeof(*slab));
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
	bug((slab->free[byte_index] & (1 << bit_index)) == 0);
	slab->free[byte_index] &= ~(1 << bit_index);

	/* Now call the destructor and return the slab the free happened on */
	if (cache->dtor)
		cache->dtor(obj);
	slab->in_use--;
	return slab;
}

static inline void slab_cache_lock(struct slab_cache* cache, irqflags_t* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		spinlock_lock_irq_save(&cache->spinlock, irq_flags);
	else
		mutex_lock(&cache->mutex);
}

static inline void slab_cache_unlock(struct slab_cache* cache, irqflags_t* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		spinlock_unlock_irq_restore(&cache->spinlock, irq_flags);
	else
		mutex_unlock(&cache->mutex);
}

void* slab_cache_alloc(struct slab_cache* cache) {
	irqflags_t irq_flags;
	slab_cache_lock(cache, &irq_flags);

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
	bug(ret == NULL); /* If this happens something bad has happened since the cache grew successfuly */

	/* Check to see if the slab is in the appropriate list. If not, move it. */
	if (slab->in_use == 1) {
		list_remove(&slab->link);
		list_add(&cache->partial, &slab->link);
	} else if (slab->in_use == cache->obj_count) {
		list_remove(&slab->link);
		list_add(&cache->full, &slab->link);
	}

out:
	slab_cache_unlock(cache, &irq_flags);
	return ret;
}

void slab_cache_free(struct slab_cache* cache, void* obj) {
	irqflags_t irq_flags;
	slab_cache_lock(cache, &irq_flags);

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
	slab_cache_unlock(cache, &irq_flags);
}

struct slab_cache* slab_cache_create(size_t obj_size, size_t align, 
		mm_t mm_flags, void (*ctor)(void*), void (*dtor)(void*)) {
	if (obj_size == 0)
		return NULL;

	if (align == 0)
		align = 8;
	else if (align & (align - 1))
		return NULL;

	struct slab_cache* cache;
	physaddr_t _cache = alloc_pages(MM_ZONE_NORMAL | (mm_flags & MM_ATOMIC), get_order(sizeof(*cache)));
	if (!_cache)
		return NULL;

	cache = hhdm_virtual(_cache);
	cache->ctor = ctor;
	cache->dtor = dtor;
	list_head_init(&cache->full);
	list_head_init(&cache->partial);
	list_head_init(&cache->empty);
	cache->obj_size = ROUND_UP(obj_size, align);
	cache->obj_count = cache->obj_size < SLAB_SIZE_CUTOFF ? (PAGE_SIZE * 2) / cache->obj_size : SLAB_AFTER_CUTOFF_OBJ_COUNT;
	cache->align = align;
	cache->mm_flags = mm_flags;
	if (mm_flags & MM_ATOMIC)
		spinlock_init(&cache->spinlock);
	else
		mutex_init(&cache->mutex);

	return cache;
}

static inline bool slab_cache_try_lock(struct slab_cache* cache, irqflags_t* irq_flags) {
	if (cache->mm_flags & MM_ATOMIC)
		return spinlock_try_lock_irq_save(&cache->spinlock, irq_flags);
	return mutex_try_lock(&cache->mutex);
}

int slab_cache_destroy(struct slab_cache* cache) {
	irqflags_t irq_flags;
	if (!slab_cache_try_lock(cache, &irq_flags))
		return -EWOULDBLOCK;
	if (!list_empty(&cache->partial) || !list_empty(&cache->full)) {
		slab_cache_unlock(cache, &irq_flags);
		return -EBUSY;
	}

	struct slab* slab, *tmp;
	list_for_each_entry_safe(slab, tmp, &cache->empty, link) {
		list_remove(&slab->link);
		slab_free_struct(slab->base, cache->obj_size * cache->obj_count);
		slab_free_struct(slab->free, (cache->obj_count + 7) >> 3);
		slab_free_struct(slab, sizeof(*slab));
	}

	slab_cache_unlock(cache, &irq_flags);
	free_pages(hhdm_physical(cache), get_order(sizeof(*cache)));
	return 0;
}
