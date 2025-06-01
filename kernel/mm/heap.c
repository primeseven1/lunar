#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>

#define HEAP_CHK_VALUE 0xdecafc0ffee
#define HEAP_CACHE_COUNT 12

/* Use 64 bit integers to keep 8 byte alignment */
struct alloc_info {
	u64 size, mm_flags;
};

static const size_t heap_cache_sizes[HEAP_CACHE_COUNT] = { 
	256, 256, 256, 512, 512, 512, 1024, 1024, 1024, 2048, 2048, 2048 
};
static struct slab_cache* heap_caches[HEAP_CACHE_COUNT] = { NULL };

static struct slab_cache* get_slab_cache(size_t size, mm_t mm_flags) {
	struct slab_cache* cache = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(heap_caches); i++) {
		if (size <= heap_caches[i]->obj_size && heap_caches[i]->mm_flags == mm_flags) {
			cache = heap_caches[i];
			break;
		}
	}
	return cache;
}

void* kmalloc(size_t size, mm_t mm_flags) {
	size = ROUND_UP(size, sizeof(u64));

	const size_t total_size = size + sizeof(struct alloc_info) + sizeof(size_t);
	struct slab_cache* cache = get_slab_cache(total_size, mm_flags);

	/* If there is no slab that can accomodate the size or the mm flags, it will just call kmap directly */
	struct alloc_info* alloc_info;
	if (cache) {
		alloc_info = slab_cache_alloc(cache);
		if (!alloc_info)
			return NULL;
	} else {
		alloc_info = kmap(mm_flags, total_size, MMU_READ | MMU_WRITE);
		if (!alloc_info)
			return NULL;
	}

	/* Now store the allocation info */
	alloc_info->size = size;
	alloc_info->mm_flags = mm_flags;

	u8* ret = (u8*)(alloc_info + 1);
	memset(ret, 0, size);

	size_t* check_value = (size_t*)(ret + size);
	*check_value = HEAP_CHK_VALUE;

	return ret;
}

void* krealloc(void* addr, size_t new_size, mm_t mm_flags) {
	new_size = ROUND_UP(new_size, sizeof(u64));
	struct alloc_info* old_alloc_info = (struct alloc_info*)addr - 1;

	size_t old_size = old_alloc_info->size;
	mm_t old_mm_flags = old_alloc_info->mm_flags;

	size_t* check_value = (size_t*)((u8*)addr + old_size);
	if (*check_value != HEAP_CHK_VALUE)
		panic("Kernel heap corruption! Check value: %zu", *check_value);

	const size_t new_total_size = new_size + sizeof(struct alloc_info) + sizeof(size_t);
	struct slab_cache* new_cache = get_slab_cache(new_total_size, mm_flags);

	/* Like in kmalloc, try to allocate from a slab cache, But if that's not possible, use kmap */
	struct alloc_info* new_alloc_info;
	if (new_cache) {
		new_alloc_info = slab_cache_alloc(new_cache);
		if (!new_alloc_info)
			return NULL;
	} else {
		new_alloc_info = kmap(mm_flags, new_total_size, MMU_READ | MMU_WRITE);
		if (!new_alloc_info)
			return NULL;
	}

	new_alloc_info->size = new_size;
	new_alloc_info->mm_flags = mm_flags;

	/* Now copy over memory from the old location */
	u8* ret = (u8*)(new_alloc_info + 1);
	if (new_size > old_size) {
		memset(ret, 0, new_size);
		memcpy(ret, addr, old_size);
	} else {
		memcpy(ret, addr, new_size);
	}

	check_value = (size_t*)((u8*)ret + new_size);
	*check_value = HEAP_CHK_VALUE;

	/* If old_cache is NULL, then kmap was used to allocate the block */
	struct slab_cache* old_cache = get_slab_cache(old_size, old_mm_flags);
	if (old_cache) {
		slab_cache_free(old_cache, old_alloc_info);
	} else {
		size_t old_total_size = old_size + sizeof(struct alloc_info) + sizeof(size_t);
		kunmap(old_alloc_info, old_size + old_total_size);
	}

	return ret;
}

void kfree(void* addr) {
	struct alloc_info* alloc_info = (struct alloc_info*)addr - 1;

	size_t alloc_size = alloc_info->size;
	mm_t alloc_mm_flags = alloc_info->mm_flags;

	size_t* check_value = (size_t*)((u8*)addr + alloc_size);
	if (*check_value != HEAP_CHK_VALUE)
		panic("Kernel heap corruption! Check value: %zu", *check_value);

	/* If cache is NULL, kmap was used to allocate the block */
	const size_t total_size = alloc_size + sizeof(struct alloc_info) + sizeof(size_t);
	struct slab_cache* cache = get_slab_cache(total_size, alloc_mm_flags);
	if (cache)
		slab_cache_free(cache, alloc_info);
	else
		kunmap(alloc_info, total_size);
}

void heap_init(void) {
	for (size_t i = 0; i < ARRAY_SIZE(heap_caches); i++) {
		mm_t zone;
		if (i % 3 == 0)
			zone = MM_ZONE_NORMAL;
		else if (i % 3 == 1)
			zone = MM_ZONE_DMA32;
		else
			zone = MM_ZONE_DMA;

		struct slab_cache* cache = slab_cache_create(heap_cache_sizes[i], 8, zone, NULL, NULL);
		if (!cache)
			panic("Failed to allocate slab caches for heap!");
		heap_caches[i] = cache;
	}
}
