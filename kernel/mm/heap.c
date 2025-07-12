#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>

#define HEAP_CANARY_VALUE 0xdecafc0ffeeUL
#define HEAP_ALIGN 16

struct mempool {
	struct slab_cache* cache;
	struct mempool* prev, *next;
	atomic(unsigned long) refcount;
};

/* The cache for other mempools, initialized by heap_init */
static struct mempool* mempool_head = NULL;

/* Protects the linked list, not the mempools */
static spinlock_t mempool_spinlock = SPINLOCK_STATIC_INITIALIZER;

/* Find a suitable mempool for an allocation, increases the refcount */
static struct mempool* walk_mempools(size_t size, mm_t mm_flags) {
	size = ROUND_UP(size, HEAP_ALIGN);

	unsigned long lock_flags;
	spinlock_lock_irq_save(&mempool_spinlock, &lock_flags);

	struct mempool* pool = mempool_head;
	while (1) {
		if (pool->cache->obj_size >= size && pool->cache->obj_size <= size + 128 && pool->cache->mm_flags == mm_flags) {
			atomic_add_fetch(&pool->refcount, 1, ATOMIC_SEQ_CST);
			spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
			return pool;
		}

		if (!pool->next)
			break;
		pool = pool->next;
	}

	/* Either there is not enough space in the current pools, or no pool matches mm_flags */
	struct mempool* new_pool = slab_cache_alloc(mempool_head->cache);
	if (!new_pool)
		goto leave;

	/* Create a new slab cache for the new pool */
	new_pool->cache = slab_cache_create(size, HEAP_ALIGN, mm_flags, NULL, NULL);
	if (!new_pool->cache) {
		slab_cache_free(mempool_head->cache, new_pool);
		new_pool = NULL;
		goto leave;
	}

	new_pool->prev = pool;
	new_pool->next = NULL;
	atomic_store(&new_pool->refcount, 1, ATOMIC_SEQ_CST);

	pool->next = new_pool;
leave:
	spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
	if (new_pool)
		printk(PRINTK_DBG "mm: Successfully created new heap pool with a size of %zu\n", size);

	return new_pool;
}

static void attempt_delete_mempool(struct mempool* pool) {
	unsigned long lock_flags;
	spinlock_lock_irq_save(&mempool_spinlock, &lock_flags);

	if (atomic_load(&pool->refcount, ATOMIC_SEQ_CST))
		goto leave;

	size_t obj_size = pool->cache->obj_size;
	int err = slab_cache_destroy(pool->cache);
	if (unlikely(err))
		goto leave;

	if (pool->next)
		pool->next->prev = pool->prev;

	/* 
	 * We know this isn't the head because the refcount on the head is never zero, 
	 * so no need to check the prev pointer in this context.
	 */
	pool->prev->next = pool->next;

	slab_cache_free(mempool_head->cache, pool);
	printk(PRINTK_DBG "mm: Successfully destroyed heap pool with a size of %zu\n", obj_size);
leave:
	spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
}

struct alloc_info {
	struct mempool* pool;
	u64 size; /* u64 for padding */
};

void* kmalloc(size_t size, mm_t mm_flags) {
	if (!size)
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
			if (atomic_sub_fetch(&pool->refcount, 1, ATOMIC_SEQ_CST) == 0)
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
	if (atomic_sub_fetch(&pool->refcount, 1, ATOMIC_SEQ_CST) == 0)
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
	/* First map a single page for the mempool */
	mempool_head = vmap(NULL, sizeof(*mempool_head), MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	assert(mempool_head != NULL);

	/* Create a cache for creating mempools */
	mempool_head->cache = slab_cache_create(sizeof(struct mempool), HEAP_ALIGN, MM_ZONE_NORMAL, NULL, NULL);
	assert(mempool_head->cache != NULL);

	mempool_head->next = NULL;
	mempool_head->prev = NULL;
	atomic_store(&mempool_head->refcount, 1, ATOMIC_RELAXED);
}
