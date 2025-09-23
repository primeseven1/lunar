#include <lunar/common.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/cpu.h>
#include <lunar/init/status.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/heap.h>
#include <lunar/mm/slab.h>
#include <lunar/lib/string.h>

#define HEAP_CANARY_XOR 0xdecafc0ffeeUL
#define HEAP_ALIGN 16
#define OBJ_SIZE_SLACK 128

struct mempool {
	struct slab_cache* cache; /* Slab cache backing */
	atomic(unsigned long) refcount; /* Will only ever increase when mempool_lock is locked */
	bool destroy_thread_exists; /* Prevents creating several deleter threads, modified/read only under the mempool lock */
	struct list_node link;
};

static LIST_HEAD_DEFINE(mempool_head);
static MUTEX_DEFINE(mempool_lock);
static struct mempool self_mempool = {
	.cache = NULL,
	.refcount = atomic_init(1),
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
			atomic_add_fetch(&p->refcount, 1);
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

	new_pool->destroy_thread_exists = false;
	atomic_store(&new_pool->refcount, 1);
	list_node_init(&new_pool->link);
	list_add(&mempool_head, &new_pool->link);
leave:
	mutex_unlock(&mempool_lock);
	if (new_pool)
		printk(PRINTK_DBG "mm: Successfully created new heap pool with a size of %zu\n", size);

	return new_pool;
}

static inline void delete_mempool(struct mempool* pool) {
	size_t cache_size = pool->cache->obj_size;
	bug(slab_cache_destroy(pool->cache) != 0);
	printk(PRINTK_DBG "mm: Successfully destroyed heap pool with a size of %zu\n", cache_size);
	list_remove(&pool->link);
	slab_cache_free(self_mempool.cache, pool);
}

static int deleter_thread(void* arg) {
	struct mempool* pool = arg;

	/* Sleep for 100 ms and then try to delete, to avoid recreating mempools constantly */
	sched_prepare_sleep(200, 0);
	schedule();

	mutex_lock(&mempool_lock);

	pool->destroy_thread_exists = false;
	if (atomic_load(&pool->refcount))
		goto out;

	delete_mempool(pool);
out:
	mutex_unlock(&mempool_lock);
	kthread_exit(0);
}

static void attempt_delete_mempool(struct mempool* pool) {
	mutex_lock(&mempool_lock);

	bool create_thread = false;
	if (atomic_load(&pool->refcount))
		goto leave;
	if (pool->destroy_thread_exists)
		goto leave;
	create_thread = true;

	if (init_status_get() >= INIT_STATUS_SCHED) {
		pool->destroy_thread_exists = true;
	} else {
		delete_mempool(pool);
		create_thread = false;
	}
leave:
	mutex_unlock(&mempool_lock);

	/* kthread_create may result in a code path that calls kmalloc, so unlock first to avoid a deadlock */
	if (create_thread) {
		tid_t id = kthread_create(0, deleter_thread, pool, "pool-deleter");
		if (id >= 0) {
			kthread_detach(id);
		} else { /* On failure, we don't really care, we'll just try again on another alloc/free */
			mutex_lock(&mempool_lock);
			pool->destroy_thread_exists = false;
			mutex_unlock(&mempool_lock);
		}
	}
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
	if (total_size <= SHRT_MAX) {
		pool = walk_mempools(total_size, mm_flags);
		if (!pool)
			return NULL;
		alloc_info = slab_cache_alloc(pool->cache);
		if (!alloc_info) {
			if (atomic_sub_fetch(&pool->refcount, 1) == 0)
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
	*canary_value = (uintptr_t)ret ^ HEAP_CANARY_XOR;

	return ret;
}

void kfree(void* ptr) {
	if (!ptr) {
		printk(PRINTK_ERR "mm: NULL pointer passed to kfree!\n");
		return;
	}

	struct alloc_info* alloc_info = (struct alloc_info*)ptr - 1;
	size_t* canary_value = (size_t*)((u8*)ptr + alloc_info->size);
	bug(*canary_value != ((uintptr_t)ptr ^ HEAP_CANARY_XOR));

	struct mempool* pool = alloc_info->pool;
	if (!pool) {
		size_t total_size;
		bug(__builtin_add_overflow(alloc_info->size, sizeof(struct alloc_info) + sizeof(size_t), &total_size) == true); /* Also a check for corruption */
		bug(vunmap(alloc_info, total_size, 0) != 0); /* Bad pointer, but it happens to be mapped since a page fault would happen if not */
	} else {
		slab_cache_free(pool->cache, alloc_info);
		if (atomic_sub_fetch(&pool->refcount, 1) == 0)
			attempt_delete_mempool(alloc_info->pool);
	}
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
	if (!self_mempool.cache)
		panic("Failed to create self mempool cache!");
}
