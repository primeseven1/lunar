#include <crescent/common.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/limine.h>
#include <crescent/core/printk.h>
#include <crescent/core/trace.h>
#include <crescent/core/panic.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/mm.h>
#include <crescent/mm/hhdm.h>
#include <crescent/lib/string.h>
#include "internal.h"

/* Must be volatile, so that way the null check doesn't get optimized away */
static volatile struct limine_mmap_request __limine_request mmap_request = {
	.request.id = LIMINE_MMAP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

/* Check if an address + size is within the memory region or not */
static bool mmap_entry_check(const struct limine_mmap_entry* entry, physaddr_t base, size_t size) {
	if (size == 0)
		return false;
	physaddr_t base_top;
	if (__builtin_add_overflow(base, size, &base_top))
		return false;
	physaddr_t entry_top;
	if (unlikely(__builtin_add_overflow(entry->base, entry->length, &entry_top)))
		panic("Bootloader provided memory map is bad!");

	return (base >= entry->base && base_top <= entry_top);
}

/* Check if a memory region is free or not */
static bool mmap_region_check(physaddr_t base, size_t size) {
	const struct limine_mmap_response* mmap = mmap_request.response;
	for (u64 i = 0; i < mmap->entry_count; i++) {
		const struct limine_mmap_entry* entry = mmap->entries[i];
		if (entry->type != LIMINE_MMAP_USABLE)
			continue;

		/* 
		 * The bootloader sanitizes the usable memory entries so no
		 * non-usable entries will overlap with usable entries, so we don't have
		 * to deal with fucked up memory maps
		 */
		if (mmap_entry_check(entry, base, size))
			return true;
	}

	return false;
}

/* Get the very last free usable address according to the memory map */
static physaddr_t mmap_get_last_usable(void) {
	physaddr_t ret = 0;

	/* The entries are guarunteed to be sorted by base address */
	struct limine_mmap_response* mmap = mmap_request.response;
	for (u64 i = 0; i < mmap->entry_count; i++) {
		struct limine_mmap_entry* entry = mmap->entries[i];
		if (entry->type == LIMINE_MMAP_USABLE)
			ret = entry->base + entry->length - 1;
	}
	return ret;
}

static u64 mmap_total_usable(void) {
	u64 ret = 0;
	struct limine_mmap_response* mmap = mmap_request.response;
	for (u64 i = 0; i < mmap->entry_count; i++) {
		struct limine_mmap_entry* entry = mmap->entries[i];
		if (entry->type == LIMINE_MMAP_USABLE)
			ret += entry->length;
	}
	return ret;
}

struct mem_area {
	physaddr_t base; /* Start of the memory area */
	u64 size; /* The size of the area rounded to a power of two */
	size_t real_size; /* The actual size of the area, usually the same as size */
	atomic(unsigned long) used_4k_blocks; /* The current amount of used blocks, atomic */
	unsigned long total_4k_blocks; /* 1 << MAX_ORDER */
	unsigned int layer_count; /* MAX_ORDER + 1 */
	struct {
		unsigned long* free_list;
		mutex_t lock;
	} pages; /* For managing the actual memory in the list */
};

static unsigned long find_first_free(unsigned long* free_list, unsigned int layer) {
	u8* free_list8 = (u8*)free_list;

	unsigned long block_count = 1ul << layer;
	unsigned long block = 0;

	const unsigned long ulong_bits = sizeof(unsigned long) * 8;

	/* First, test 1 bit at a time until alignment */
	while (block < block_count && (block_count + block - 1) % ulong_bits) {
		size_t byte_index = (block_count + block - 1) / 8;
		unsigned int bit_index = (block_count + block - 1) % 8;

		if (!(free_list8[byte_index] & (1ul << bit_index)))
			return block;

		block++;
	}

	/* Now test several bits at a time */
	size_t ulong_index = ((block_count + block - 1) / 8) / sizeof(unsigned long);
	free_list = free_list + ulong_index;
	while (block + ulong_bits < block_count) {
		if (*free_list != ULONG_MAX)
			return block + __builtin_ctzl(~(*free_list));

		block += ulong_bits;
		free_list++;
	}

	/* Now test the rest of the bits 1 at a time */
	while (block < block_count) {
		size_t byte_index = (block_count + block - 1) / 8;
		unsigned int bit_index = (block_count + block - 1) % 8;

		if ((free_list8[byte_index] & (1ul << bit_index)) == 0)
			return block;

		block++;
	}

	return ULONG_MAX;
}

static inline void __alloc_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) / 8;
	unsigned int bit_index = (block_count + block - 1) % 8;
	((u8*)free_list)[byte_index] |= (1 << bit_index);
}

static inline void __free_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) / 8;
	unsigned int bit_index = (block_count + block - 1) % 8;
	((u8*)free_list)[byte_index] &= ~(1 << bit_index);
}

static inline bool __is_block_free(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) / 8;
	unsigned int bit_index = (block_count + block - 1) % 8;
	return ((((u8*)free_list)[byte_index] & (1 << bit_index)) == 0);
}

/* Allocate blocks, but also manages the other blocks corresponding to the block on other layers */
static int _alloc_block(struct mem_area* area, unsigned int layer, unsigned long block) {
	if (layer >= area->layer_count)
		return -EINVAL;

	unsigned long block_count = 1ul << layer;
	if (block >= block_count)
		return -EFAULT;
	if (!__is_block_free(area->pages.free_list, block_count, block))
		return -EALREADY;

	__alloc_block(area->pages.free_list, block_count, block);

	unsigned long tmp = block;
	unsigned int tmp2 = layer;

	/* Allocate the bigger blocks */
	while (layer--) {
		block_count = 1ul << layer;
		block >>= 1;
		__alloc_block(area->pages.free_list, block_count, block);
	}

	block = tmp;
	layer = tmp2;

	/* Allocate the smaller blocks below the layer we allocated on */
	unsigned long times = 2;
	while (++layer < area->layer_count) {
		block_count = 1ul << layer;
		block <<= 1;
		for (unsigned long i = 0; i < times; i++)
			__alloc_block(area->pages.free_list, block_count, block + i);
		times <<= 1;
	}

	return 0;
}

/* Frees blocks, but also manages the other blocks corresponding to the block on other layers */
static int _free_block(struct mem_area* area, unsigned int layer, unsigned long block) {
	if (layer >= area->layer_count)
		return -EINVAL;

	unsigned long block_count = 1ul << layer;
	if (block >= block_count)
		return -EFAULT;
	if (__is_block_free(area->pages.free_list, block_count, block))
		return -EALREADY;

	unsigned long tmp = block;
	unsigned int tmp2 = layer;

	while (1) {
		/* First free the current block */
		__free_block(area->pages.free_list, block_count, block);

		/* Layer 0 has no buddy */
		if (layer == 0)
			break;

		/* 
		 * Now check the buddy, if it's free, then the two blocks can be merged,
		 * otherwise this part of the free process is done
		 */
		unsigned long buddy = block & 1 ? block - 1 : block + 1;
		if (!__is_block_free(area->pages.free_list, block_count, buddy))
			break;

		/* Now just move on to the next layer */
		layer--;
		block >>= 1;
		block_count = 1ul << layer;
	}

	block = tmp;
	layer = tmp2;

	/* Now finish by freeing the smaller blocks below the original layer we freed on */
	unsigned long count = 2;
	while (++layer < area->layer_count) {
		block_count = 1ul << layer;
		block <<= 1;
		for (unsigned long i = 0; i < count; i++)
			__free_block(area->pages.free_list, block_count, block + i);
		count <<= 1;
	}

	return 0;
}

struct zone {
	mm_t zone_type; /* Has only 1 flag, either MM_ZONE_DMA, MM_ZONE_DMA32, or MM_ZONE_NORMAL */
	unsigned long area_count; /* The number of areas the zone has */
	struct mem_area* areas; /* The array of memory areas, in order by area->base */
};

/*
 * Selects a memory area based on which area has the least amount of allocated blocks.
 *
 * This function will also try to allocate the requested size to see if the area
 * has enough contiguous blocks. It will also return the layer and the block it finds,
 * since there is no reason not to do that.
 *
 * Acquires area->lock
 */
static struct mem_area* select_mem_area(struct zone* zone, unsigned int order, unsigned int* layer, unsigned long* block) {
	struct mem_area* best = &zone->areas[0];

	unsigned long i = 1;
	unsigned long block_count = (PAGE_SIZE << order) >> PAGE_SHIFT;

	unsigned long timeout = 20;
	while (timeout--) {
		if (i == zone->area_count - 1)
			i = 0;
		for (; i < zone->area_count; i++) {
			struct mem_area* a = &zone->areas[i];
			unsigned long used_4k = atomic_load(&a->used_4k_blocks, ATOMIC_SEQ_CST);
			unsigned long free = a->total_4k_blocks - used_4k;
			if (free >= block_count && used_4k < atomic_load(&best->used_4k_blocks, ATOMIC_SEQ_CST))
				best = a;
		}

		mutex_lock(&best->pages.lock);
		*layer = best->layer_count - order - 1u;
		*block = find_first_free(best->pages.free_list, *layer);
		if (*block != ULONG_MAX)
			return best;

		/*
		 * Reaching here means that either there was no contiguous block,
		 * or somebody got to this area before we did
		 */
		mutex_unlock(&best->pages.lock);
	}

	return NULL;
}

/* Get a memory area based on an address. This function performs no locking, unlike select_mem_area. */
static struct mem_area* get_mem_area(struct zone* zone, physaddr_t addr) {
	struct mem_area* areas = zone->areas;
	for (unsigned long i = 0; i < zone->area_count; i++) {
		physaddr_t area_end = areas[i].base + areas[i].total_4k_blocks * PAGE_SIZE;
		if (addr >= areas[i].base && addr < area_end)
			return &areas[i];
	}

	return NULL;
}

/*
 * Allocate pages from a memory zone, this function makes sure the memory
 * region is actually system RAM before returning the address.
 */
static physaddr_t __alloc_pages(struct zone* zone, unsigned int order) {
	size_t alloc_size = PAGE_SIZE << order;
	unsigned long block4k_count = alloc_size >> PAGE_SHIFT;

	physaddr_t ret = 0;
	int err;

	unsigned int layer;
	unsigned long block;
	struct mem_area* area = select_mem_area(zone, order, &layer, &block);
	if (!area)
		goto out;

retry:
	ret = 0;

	/* 
	 * On the first attempt, select_mem_area will select a block for us. If we need a retry, 
	 * block is set to ULONG_MAX so that way we know to actually retry.
	 */
	if (block == ULONG_MAX) {
		block = find_first_free(area->pages.free_list, layer);
		if (block == ULONG_MAX) {
			mutex_unlock(&area->pages.lock);
			area = select_mem_area(zone, order, &layer, &block);
			if (!area)
				goto out;
		}
	}

	err = _alloc_block(area, layer, block);
	if (err) {
		printk(PRINTK_ERR "mm: _alloc_block failed in %s with error code %i\n", __func__, err);
		goto out;
	}

	ret = area->base + (block * alloc_size);

	/* Since the blocks that could be outside of the area are allocated, this should not happen */
	if (unlikely(ret + alloc_size > area->base + area->real_size)) {
		printk(PRINTK_ERR "mm: Tried allocating a block outside of area!\n");
		_free_block(area, layer, block);
		ret = 0;
		goto out;
	}

	atomic_add_fetch(&area->used_4k_blocks, block4k_count, ATOMIC_SEQ_CST);

	/* Now make sure this region is actually marked usable in the memory map */
	if (!mmap_region_check(ret, alloc_size)) {
		/* Go through each page, and see what usable pages there are, and free those blocks */
		for (size_t i = 0; i < alloc_size; i += PAGE_SIZE) {
			if (mmap_region_check(ret + i, PAGE_SIZE)) {
				block = ((ret + i) - area->base) >> PAGE_SHIFT;
				_free_block(area, area->layer_count - 1, block);
				atomic_sub_fetch(&area->used_4k_blocks, 1, ATOMIC_SEQ_CST);
			}
		}

		block = ULONG_MAX;
		goto retry;
	}
out:
	if (area)
		mutex_unlock(&area->pages.lock);
	return ret;
}

/* Free pages from a specific memory zone. */
static int __free_pages(struct zone* zone, physaddr_t addr, unsigned int order) {
	struct mem_area* area = get_mem_area(zone, addr);
	if (!area)
		return -EFAULT;

	unsigned int layer = area->layer_count - order - 1;
	size_t alloc_size = PAGE_SIZE << order;
	unsigned long block4k_count = alloc_size >> PAGE_SHIFT;
	unsigned long block = (addr - area->base) / alloc_size;

	mutex_lock(&area->pages.lock);

	int ret = _free_block(area, layer, block);
	if (ret)
		goto cleanup;
	atomic_sub_fetch(&area->used_4k_blocks, block4k_count, ATOMIC_SEQ_CST);
cleanup:
	mutex_unlock(&area->pages.lock);
	return ret;
}

/* DMA zone is maxed out to 16MiB, so we know 12 is the maximum */
#define DMA_MAX_ORDER 12

/* 
 * Since the DMA zone is maxed out to 16MiB, so this can be created statically 
 * since the free list will only be a couple of pages 
 */
static struct mem_area dma_area;
static unsigned long dma_area_free_list[((1 << (DMA_MAX_ORDER + 1)) / sizeof(unsigned long))];
static struct zone dma_zone;

/* 
 * __dma32_zone and __normal_zone are not guarunteed to be used, The dma32_zone and 
 * normal_zone pointers can be linked with other zones if there is not enough space for them.
 */
static struct zone __dma32_zone;
static struct zone __normal_zone;
static struct zone* dma32_zone;
static struct zone* normal_zone;

/* 
 * Get a memory zone from MM flags, this function will allocate
 * select the most restrictive zone if multiple zones are set.
 */
static struct zone* get_zone_mm(mm_t mm_flags) {
	if (mm_flags & MM_ZONE_DMA)
		return &dma_zone;
	if (mm_flags & MM_ZONE_DMA32)
		return dma32_zone;
	if (mm_flags & MM_ZONE_NORMAL)
		return normal_zone;
	return NULL;
}

/* Get a memory zone from a physical memory address */
static struct zone* get_zone_addr(physaddr_t addr, size_t size) {
	physaddr_t addr_top;
	if (__builtin_add_overflow(addr, size, &addr_top))
		return NULL;

	/* Only one area for the DMA zone */
	if (addr >= dma_area.base && addr_top <= dma_area.base + dma_area.real_size)
		return &dma_zone;

	/* This code is not affected whether or not the first area is the same as the last area */
	struct mem_area* first_area = &dma32_zone->areas[0];
	struct mem_area* last_area = &dma32_zone->areas[dma32_zone->area_count - 1];
	if (addr >= first_area->base && addr_top <= last_area->base + last_area->real_size)
		return dma32_zone;

	first_area = &normal_zone->areas[0];
	last_area = &normal_zone->areas[normal_zone->area_count - 1];
	if (addr >= first_area->base && addr_top <= last_area->base + last_area->real_size)
		return normal_zone;

	return NULL;
}

static u64 mem_in_use = 0;
static u64 mem_total = 0;

u64 get_free_memory(u64* total) {
	*total = mem_total;
	return mem_in_use;
}

physaddr_t alloc_pages(mm_t mm_flags, unsigned int order) {
	if (order >= MAX_ORDER) {
		printk(PRINTK_ERR "mm: order (%u) >= MAX_ORDER (%u) in %s\n", order, MAX_ORDER, __func__);
		dump_stack();
		return 0;
	}

	struct zone* zone = get_zone_mm(mm_flags);
	if (!zone) {
		printk(PRINTK_ERR "mm: bad flags passed to %s, flags: %u\n", __func__, mm_flags);
		dump_stack();
		return 0;
	}

	/* Have retries here, so that way we can see if another thread frees any memory */
	const unsigned int max_retries = 10;
	unsigned int retries = max_retries;
	do {
		physaddr_t physical = __alloc_pages(zone, order);
		if (physical) {
			mem_in_use += (1u << order) << PAGE_SHIFT;
			return physical;
		}

		if (mm_flags & MM_NOFAIL && retries == 0) {
			out_of_memory();
			retries = max_retries;
			continue;
		} else if (retries < max_retries / 2) {
			switch (zone->zone_type) {
			case MM_ZONE_NORMAL:
				zone = dma32_zone;
				break;
			case MM_ZONE_DMA32:
				zone = &dma_zone;
				break;
			default:
				break;
			}
		}
	} while (retries--);

	return 0;
}

void free_pages(physaddr_t addr, unsigned int order) {
	int err = 0;
	if (order >= MAX_ORDER || addr % PAGE_SIZE || addr < PAGE_SIZE) {
		err = -EINVAL;
		goto err;
	}

	size_t alloc_size = PAGE_SIZE << order;
	struct zone* zone = get_zone_addr(addr, alloc_size);
	if (!zone) {
		err = -EFAULT;
		goto err;
	}

	err = __free_pages(zone, addr, order);
	if (err)
		goto err;

	mem_in_use -= (1u << order) << PAGE_SHIFT;
	return;
err:
	switch (err) {
	case -EFAULT:
		printk(PRINTK_ERR "mm: %s Tried to free bad address (%#.16lx)\n", __func__, addr);
		break;
	case -EALREADY:
		printk(PRINTK_ERR "mm: %s tried to free an address that was already free (%#.16lx)\n", __func__, addr);
		break;
	case -EINVAL:
		if (order >= MAX_ORDER)
			printk(PRINTK_ERR "mm: order (%u) >= MAX_ORDER (%u) in %s\n", order, MAX_ORDER, __func__);
		if (addr % PAGE_SIZE)
			printk(PRINTK_ERR "mm: Misaligned address passed to %s (%#.16lx)\n", __func__, addr);
		if (addr < PAGE_SIZE)
			printk(PRINTK_ERR "mm: %s tried to free the first page of memory\n", __func__);
		break;
	default:
		printk(PRINTK_ERR "mm: %s unknown error (%i)\n", __func__, err);
		break;
	}
	dump_stack();
}

static u64 round_power2(u64 base, u64 x) {
	if (x < base || base == 0)
		return base;

	u64 ret = base;
	while (ret < x)
		ret <<= 1;
	return ret;
}

static unsigned int get_layer_count(size_t size) {
	unsigned int layers = 1;
	while (layers <= MAX_ORDER) {
		size_t block_size = size >> (layers - 1);
		if ((block_size / 2) < PAGE_SIZE)
			break;
		layers++;
	}
	return layers;
}

/* Initializes the DMA zone, all structures for this zone are statically allocated */
static void dma_zone_init(physaddr_t last_usable) {
	dma_zone.zone_type = MM_ZONE_DMA;

	size_t dma_size = 0x1000000;
	if (unlikely(last_usable < dma_size))
		dma_size = last_usable;

	dma_zone.area_count = 1;
	dma_area.base = 0;
	dma_area.pages.free_list = dma_area_free_list;
	dma_area.size = round_power2(PAGE_SIZE, dma_size);
	dma_area.real_size = dma_size;
	dma_zone.areas = &dma_area;

	dma_area.layer_count = get_layer_count(dma_size);
	dma_area.total_4k_blocks = 1 << (dma_area.layer_count - 1);
	memset(dma_area_free_list, 0, sizeof(dma_area_free_list));

	/* Allocate the first page of memory, an error should never happen in this context */
	assert(_alloc_block(&dma_area, dma_area.layer_count - 1, 0) == 0);
}

/* 
 * Initialize a memory area, free_list_zone is the zone the free list should be allocated from,
 * so this function can be used when not every zone is initialized yet.
 */
static int init_area(struct mem_area* area, mm_t free_list_zone, physaddr_t base, size_t real_size) {
	u64 rounded_size = round_power2(PAGE_SIZE, real_size);
	unsigned int layer_count = get_layer_count(rounded_size);
	if (layer_count == 1)
		return -ELOOP;

	size_t free_list_size = (1 << layer_count) + 1;
	physaddr_t free_list = alloc_pages(free_list_zone, get_order(free_list_size));
	if (!free_list)
		return -ENOMEM;

	area->base = base;
	area->real_size = real_size;
	area->size = rounded_size;
	area->layer_count = layer_count;
	area->total_4k_blocks = 1 << (layer_count - 1);
	atomic_store(&area->used_4k_blocks, 0, ATOMIC_RELAXED);
	area->pages.free_list = hhdm_virtual(free_list);
	mutex_init(&area->pages.lock);
	memset(area->pages.free_list, 0, free_list_size);

	/* Allocate the invalid area, if there is one */
	if (rounded_size != real_size) {
		unsigned long start_block = real_size / PAGE_SIZE;
		unsigned long end_block = rounded_size / PAGE_SIZE;
		for (unsigned long block = start_block; block < end_block; block++)
			assert(_alloc_block(area, layer_count - 1, block) == 0);
	}
	return 0;
}

/* Initialize a memory zone, alloc_zone is the zone to allocate the memory structures from */
static int zone_init(struct zone* zone, mm_t zone_type, mm_t alloc_zone, 
		physaddr_t last_usable, physaddr_t min_start, physaddr_t max_end) {
	if (last_usable < min_start)
		return -ELOOP;
	if (last_usable < max_end)
		max_end = last_usable;

	size_t zone_size = max_end - min_start;
	size_t max_area_size = PAGE_SIZE << (MAX_ORDER);

	/* First allocate the array of areas for the zone */
	unsigned long area_count = zone_size / max_area_size;
	if (unlikely(area_count == 0))
		area_count = 1;

	unsigned int area_order = get_order(sizeof(struct mem_area) * area_count);
	physaddr_t _areas = alloc_pages(alloc_zone, area_order);
	if (!_areas)
		return -ENOMEM;
	struct mem_area* areas = hhdm_virtual(_areas);

	size_t rest = zone_size;
	for (unsigned long i = 0; i < area_count; i++) {
		size_t area_size = rest > max_area_size ? max_area_size : rest;
		int err = init_area(&areas[i], alloc_zone, min_start, area_size);

		/* -ELOOP here means that the area is just too small, so just don't bother with it */
		if (err == -ELOOP) {
			if (unlikely(--area_count == 0)) {
				free_pages(_areas, area_order);
				return err;
			}
			break;
		} else if (err) {
			return err;
		}

		/* Now adjust the sizes for the next area */
		min_start += area_size;
		rest -= area_size;
	}

	zone->area_count = area_count;
	zone->zone_type = zone_type;
	zone->areas = areas;

	return 0;
}

void buddy_init(void) {
	mem_total = mmap_total_usable();

	physaddr_t last_usable = mmap_get_last_usable();
	dma_zone_init(last_usable);

	/* Any other error the -ELOOP here typically means a problem with the allocator */
	int err = zone_init(&__dma32_zone, MM_ZONE_DMA32, MM_ZONE_DMA, last_usable, 0x1000000, 0x100000000);
	if (unlikely(err == -ELOOP)) {
		dma32_zone = &dma_zone;
		normal_zone = &dma_zone;
		printk(PRINTK_DBG "mm: Linking dma32 and normal zones to the dma zone\n");
		return;
	} else {
		assert(err == 0);
	}
	dma32_zone = &__dma32_zone;

	err = zone_init(&__normal_zone, MM_ZONE_NORMAL, MM_ZONE_DMA32, last_usable, 0x100000000, PHYSADDR_MAX);
	if (err == -ELOOP) {
		normal_zone = dma32_zone;
		printk(PRINTK_DBG "mm: Linking normal zone to the dma32 zone\n");
		return;
	} else {
		assert(err == 0);
	}
	normal_zone = &__normal_zone;
}
