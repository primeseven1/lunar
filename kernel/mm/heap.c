#include <lunar/common.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/cpu.h>
#include <lunar/init/status.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/heap.h>
#include <lunar/mm/hhdm.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/slab.h>
#include <lunar/lib/string.h>

#define HEAP_CANARY_XOR 0xdecafc0ffeeUL
#define HEAP_ALIGN BIGGEST_ALIGNMENT

struct alloc_info {
	struct slab_cache* cache;
	u64 size, atomic, __pad;
} __attribute__((aligned(16)));
static_assert((sizeof(struct alloc_info) & (HEAP_ALIGN - 1)) == 0,"struct alloc_info is broken");

#define CACHE_COUNT 27
static struct slab_cache* normal_caches[CACHE_COUNT];
static struct slab_cache* atomic_caches[CACHE_COUNT];

static struct slab_cache* get_cache(size_t size, mm_t mm_flags) {
	struct slab_cache** caches = normal_caches;
	size_t count = ARRAY_SIZE(normal_caches);
	if (mm_flags & MM_ATOMIC) {
		caches = atomic_caches;
		count = ARRAY_SIZE(atomic_caches);
	}

	for (size_t i = 0; i < count; i++) {
		if (mm_flags == caches[i]->mm_flags && size <= caches[i]->obj_size)
			return caches[i];
	}

	return NULL;
}

void* kmalloc(size_t size, mm_t mm_flags) {
	if (!size)
		return NULL;
	if (size >= SIZE_MAX - HEAP_ALIGN)
		return NULL;
	size = ROUND_UP(size, HEAP_ALIGN);

	size_t total_size;
	if (__builtin_add_overflow(size, sizeof(struct alloc_info) + sizeof(size_t), &total_size))
		return NULL;

	bool atomic = mm_flags & MM_ATOMIC;
	struct alloc_info* ai = NULL;
	struct slab_cache* cache = get_cache(total_size, mm_flags);
	if (cache)
		ai = slab_cache_alloc(cache);
	if (!ai) {
		ai = atomic ?
			hhdm_virtual(alloc_pages(mm_flags, get_order(total_size))) :
			vmap(NULL, total_size, MMU_READ | MMU_WRITE, VMM_ALLOC, &mm_flags);
		if (!ai)
			return NULL;
	}

	ai->cache = cache;
	ai->size = size;
	ai->atomic = (u64)atomic;

	u8* ret = (u8*)(ai + 1);
	size_t* canary = (size_t*)(ret + size);
	*canary = (uintptr_t)ret ^ HEAP_CANARY_XOR;
	return ret;
}

void kfree(void* ptr) {
	if (!ptr)
		return;

	struct alloc_info* ai = (struct alloc_info*)ptr - 1;
	size_t* canary = (size_t*)((u8*)ptr + ai->size);
	bug(*canary != ((uintptr_t)ptr ^ HEAP_CANARY_XOR));

	size_t total_size;
	bug(__builtin_add_overflow(ai->size, sizeof(struct alloc_info) + sizeof(size_t), &total_size) == true);
	if (ai->cache) {
		slab_cache_free(ai->cache, ai);
	} else {
		if (ai->atomic)
			free_pages(hhdm_physical(ai), get_order(total_size));
		else
			bug(vunmap(ai, total_size, 0) != 0);
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

static inline mm_t next_zone(mm_t zone) {
	switch (zone) {
	case MM_ZONE_DMA:
		return MM_ZONE_DMA32;
	case MM_ZONE_DMA32:
		return MM_ZONE_NORMAL;
	default:
		return MM_ZONE_DMA;
	}
}

static bool cache_set_init(struct slab_cache** caches, size_t count, mm_t mm_extra) {
	size_t size = 256;
	mm_t zone = MM_ZONE_DMA;
	for (size_t i = 0; i < count; i++) {
		caches[i] = slab_cache_create(size, HEAP_ALIGN, zone | mm_extra, NULL, NULL);
		if (!caches[i])
			return false;
		zone = next_zone(zone);
		if (zone == MM_ZONE_DMA && i != 0)
			size *= 2;
	}

	return true;
}

void heap_init(void) {
	if (!cache_set_init(normal_caches, ARRAY_SIZE(normal_caches), 0) ||
			!cache_set_init(atomic_caches, ARRAY_SIZE(atomic_caches), MM_ATOMIC))
		panic("Failed to create heap memory caches");
	atomic_thread_fence(ATOMIC_RELEASE);
}
