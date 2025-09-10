#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>

#define HEAP_CANARY_VALUE 0xdecafc0ffeeUL
#define HEAP_ALIGN 16
#define OBJ_SIZE_SLACK 128

struct mempool {
	struct slab_cache* cache;
	atomic(unsigned long) refcount;
	struct list_node link;
};

static LIST_HEAD_DEFINE(mempool_head);
static MUTEX_DEFINE(mempool_lock);
static struct mempool self_mempool = {
	.cache = NULL,
	.refcount = atomic_static_init(1),
	.link = LIST_NODE_INITIALIZER
};

/* Find a suitable mempool for an allocation, increases the refcount */
static struct mempool* walk_mempools(size_t size, mm_t mm_flags) {
	size = ROUND_UP(size, HEAP_ALIGN);

	mutex_lock(&mempool_lock);

	struct list_node* pos;
	list_for_each(pos, &mempool_head) {
		struct mempool* p = list_entry(pos, struct mempool, link);
		if (p->cache->obj_size >= size && 
				p->cache->obj_size <= size + OBJ_SIZE_SLACK && 
				p->cache->mm_flags == mm_flags) {
			atomic_add_fetch(&p->refcount, 1, ATOMIC_RELEASE);
			mutex_unlock(&mempool_lock);
			return p;
		}
	}

	/* Either there is not enough space in the current pools, or no pool matches mm_flags */
	struct mempool* new_pool = slab_cache_alloc(self_mempool.cache);
	if (!new_pool)
		goto leave;

	/* Create a new slab cache for the new pool */
	new_pool->cache = slab_cache_create(size, HEAP_ALIGN, mm_flags, NULL, NULL);
	if (!new_pool->cache) {
		slab_cache_free(self_mempool.cache, new_pool);
		new_pool = NULL;
		goto leave;
	}

	list_add(&mempool_head, &new_pool->link);
	atomic_store(&new_pool->refcount, 1, ATOMIC_RELEASE);
leave:
	mutex_unlock(&mempool_lock);
	if (new_pool)
		printk(PRINTK_DBG "mm: Successfully created new heap pool with a size of %zu\n", size);

	return new_pool;
}

static void attempt_delete_mempool(struct mempool* pool) {
	mutex_lock(&mempool_lock);

	if (atomic_load(&pool->refcount, ATOMIC_ACQUIRE))
		goto leave;

	size_t obj_size = pool->cache->obj_size;
	int err = slab_cache_destroy(pool->cache);
	if (unlikely(err)) {
		assert(err != -EWOULDBLOCK);
		goto leave;
	}

	list_remove(&pool->link);
	slab_cache_free(self_mempool.cache, pool);
	printk(PRINTK_DBG "mm: Successfully destroyed heap pool with a size of %zu\n", obj_size);
leave:
	mutex_unlock(&mempool_lock);
}

struct alloc_info {
	struct mempool* pool;
	u64 size; /* u64 for padding */
};

void* kmalloc(size_t size, mm_t mm_flags) {
	if (!size)
		return NULL;
	if (size >= SIZE_MAX - HEAP_ALIGN)
		return NULL;
	size = ROUND_UP(size, HEAP_ALIGN);

	size_t total_size;
	if (__builtin_add_overflow(size, sizeof(struct alloc_info) + sizeof(size_t), &total_size))
		return NULL;

	struct alloc_info* alloc_info;
	struct mempool* pool = NULL;
	if (size <= SHRT_MAX) {
		pool = walk_mempools(total_size, mm_flags);
		if (!pool)
			return NULL;
		alloc_info = slab_cache_alloc(pool->cache);
		if (!alloc_info) {
			if (atomic_sub_fetch(&pool->refcount, 1, ATOMIC_ACQ_REL) == 0)
				attempt_delete_mempool(pool);
			return NULL;
		}
	} else {
		alloc_info = vmap(NULL, total_size, MMU_READ | MMU_WRITE, VMM_ALLOC, &mm_flags);
		if (!alloc_info)
			return NULL;
	}

	alloc_info->pool = pool;
	alloc_info->size = size;

	u8* ret = (u8*)(alloc_info + 1);

	size_t* canary_value = (size_t*)(ret + size);
	*canary_value = HEAP_CANARY_VALUE;

	return ret;
}

void kfree(void* ptr) {
	struct alloc_info* alloc_info = (struct alloc_info*)ptr - 1;

	size_t* canary_value = (size_t*)((u8*)ptr + alloc_info->size);
	assert(*canary_value == HEAP_CANARY_VALUE);

	struct mempool* pool = alloc_info->pool;
	if (!pool) {
		size_t total_size;
		assert(__builtin_add_overflow(alloc_info->size, sizeof(struct alloc_info) + sizeof(size_t), &total_size) == false);
		assert(vunmap(alloc_info, total_size, 0) == 0);
		return;
	}

	slab_cache_free(pool->cache, alloc_info);
	if (atomic_sub_fetch(&pool->refcount, 1, ATOMIC_ACQ_REL) == 0)
		attempt_delete_mempool(alloc_info->pool);
}

void* krealloc(void* old, size_t new_size, mm_t mm_flags) {
	if (!old)
		return kmalloc(new_size, mm_flags);
	if (!new_size) {
		kfree(old);
		return NULL;
	}

	new_size = ROUND_UP(new_size, HEAP_ALIGN);

	struct alloc_info* old_alloc_info = (struct alloc_info*)old - 1;
	size_t old_size = old_alloc_info->size;
	if (old_size == new_size && old_alloc_info->pool->cache->mm_flags == mm_flags)
		return old;

	void* new = kmalloc(new_size, mm_flags);
	if (!new)
		return NULL;

	size_t copy_size = old_size < new_size ? old_size : new_size;
	memcpy(new, old, copy_size);

	kfree(old);
	return new;
}

void heap_init(void) {
	self_mempool.cache = slab_cache_create(sizeof(struct mempool), HEAP_ALIGN, MM_ZONE_NORMAL, NULL, NULL);
	assert(self_mempool.cache != NULL);
}
