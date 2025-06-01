#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>

#define HEAP_CHK_VALUE 0xdecafc0ffeeUL

struct mempool {
	struct slab_cache* cache;
	struct mempool* prev, *next;
};

/* The cache for other mempools, initialized by heap_init */
static struct mempool* mempool_head = NULL;
static spinlock_t mempool_spinlock = SPINLOCK_INITIALIZER;

static struct mempool* walk_mempools(size_t size, mm_t mm_flags) {
	size = ROUND_UP(size, 16);

	unsigned long lock_flags;
	spinlock_lock_irq_save(&mempool_spinlock, &lock_flags);

	struct mempool* pool = mempool_head;
	while (1) {
		if (pool->cache->obj_size >= size && pool->cache->mm_flags == mm_flags) {
			spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
			return pool;
		}

		if (!pool->next)
			break;
		pool = pool->next;
	}

	/* Not enough space, so create a new pool */
	struct mempool* new_pool = slab_cache_alloc(mempool_head->cache);
	if (!new_pool)
		goto leave;

	/* Create a new slab cache for the new pool */
	new_pool->cache = slab_cache_create(size, 16, mm_flags, NULL, NULL);
	if (!new_pool->cache) {
		slab_cache_free(mempool_head->cache, new_pool);
		new_pool = NULL;
		goto leave;
	}

	new_pool->prev = pool;
	new_pool->next = NULL;
	pool->next = new_pool;
leave:
	spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
	if (new_pool)
		printk(PRINTK_DBG "mm: Successfully created new heap pool with a size of %zu\n", size);

	return new_pool;
}

static void attempt_delete_mempool(struct mempool* pool) {
	if (pool == mempool_head)
		return;

	unsigned long lock_flags;
	spinlock_lock_irq_save(&mempool_spinlock, &lock_flags);

	size_t obj_size = pool->cache->obj_size;
	int err = slab_cache_destroy(pool->cache);
	if (err)
		goto leave;

	if (pool->next)
		pool->next->prev = NULL;
	if (pool->prev)
		pool->prev->next = pool->next;

	slab_cache_free(mempool_head->cache, pool);
	pool = NULL;
leave:
	if (!pool)
		printk(PRINTK_DBG "mm: Successfully destroyed heap pool with a size of %zu\n", obj_size);
	spinlock_unlock_irq_restore(&mempool_spinlock, &lock_flags);
}

struct alloc_info {
	struct mempool* pool;
	u64 size;
};

void* kmalloc(size_t size, mm_t mm_flags) {
	size = ROUND_UP(size, 16);

	size_t total_size;
	if (__builtin_add_overflow(size, sizeof(struct alloc_info) + sizeof(size_t), &total_size))
		return NULL;

	struct mempool* pool = walk_mempools(total_size, mm_flags);
	if (!pool)
		return NULL;
	struct alloc_info* alloc_info = slab_cache_alloc(pool->cache);
	if (!alloc_info)
		return NULL;

	alloc_info->pool = pool;
	alloc_info->size = size;

	u8* ret = (u8*)(alloc_info + 1);
	memset(ret, 0, size);

	size_t* check_value = (size_t*)(ret + size);
	*check_value = HEAP_CHK_VALUE;

	return ret;
}

void kfree(void* ptr) {
	struct alloc_info* alloc_info = (struct alloc_info*)ptr - 1;

	/* Check for possible heap corruption */
	size_t* check_value = (size_t*)((u8*)ptr + alloc_info->size);
	if (*check_value != HEAP_CHK_VALUE) {
		panic("Kernel heap corruption! check_value: %lu expected: %lu", 
				*check_value, HEAP_CHK_VALUE);
	}

	slab_cache_free(alloc_info->pool->cache, alloc_info);
	attempt_delete_mempool(alloc_info->pool);
}

void* krealloc(void* old, size_t new_size, mm_t mm_flags) {
	new_size = ROUND_UP(new_size, 16);

	struct alloc_info* old_alloc_info = (struct alloc_info*)old - 1;
	size_t old_size = old_alloc_info->size;
	if (old_size == new_size)
		return old;

	void* new = kmalloc(new_size, mm_flags);
	if (!new)
		return NULL;

	if (new_size > old_size) {
		memset(new, 0, new_size);
		memcpy(new, old, old_size);
	} else {
		memcpy(new, old, new_size);
	}

	kfree(old);
	return new;
}

void heap_init(void) {
	/* First map a single page for the mempool */
	mempool_head = kmap(MM_ZONE_NORMAL, sizeof(*mempool_head), MMU_READ | MMU_WRITE);
	if (!mempool_head)
		panic("Failed to initialize initial heap pool!");

	/* Create a cache for creating mempools */
	mempool_head->cache = slab_cache_create(sizeof(struct mempool), 16, MM_ZONE_NORMAL, NULL, NULL);
	if (!mempool_head->cache)
		panic("Failed to create mempool cache!");

	mempool_head->next = NULL;
	mempool_head->prev = NULL;
}
