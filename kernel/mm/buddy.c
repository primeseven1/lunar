#include <lunar/common.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/limine.h>
#include <lunar/core/printk.h>
#include <lunar/core/trace.h>
#include <lunar/core/panic.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/mm.h>
#include <lunar/mm/hhdm.h>
#include <lunar/lib/string.h>
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
	u64 real_size; /* The actual size of the area, usually the same as size */
	atomic(unsigned long) free_blocks[MAX_ORDER + 1]; /* Amount of free blocks on every layer */
	unsigned long total_blocks; /* Number of blocks the last layer */
	unsigned int layer_count; /* Usually MAX_ORDER + 1 */
	struct {
		unsigned long* free_list;
		bool atomic; /* Can allocations from this area sleep? */
		union {
			mutex_t mutex;
			spinlock_t spinlock;
		};
	} pages; /* For managing the actual memory in the list */
	atomic(unsigned int) alloc_refcnt; /* How many threads are allocating from this area */
};

static inline void mem_area_lock(struct mem_area* area, irqflags_t* irq_flags) {
	if (area->pages.atomic)
		spinlock_lock_irq_save(&area->pages.spinlock, irq_flags);
	else
		mutex_lock(&area->pages.mutex);
}

static inline void mem_area_unlock(struct mem_area* area, irqflags_t* irq_flags) {
	if (area->pages.atomic)
		spinlock_unlock_irq_restore(&area->pages.spinlock, irq_flags);
	else
		mutex_unlock(&area->pages.mutex);
}

static unsigned long find_first_free(unsigned long* free_list, unsigned int layer) {
	u8* free_list8 = (u8*)free_list;

	unsigned long block_count = 1ul << layer;
	unsigned long block = 0;

	const unsigned long ulong_bits = sizeof(unsigned long) * 8;

	/* First, test 1 bit at a time until alignment */
	while (block < block_count && (block_count + block - 1) % ulong_bits) {
		size_t byte_index = (block_count + block - 1) >> 3;
		unsigned int bit_index = (block_count + block - 1) & 7;

		if (!(free_list8[byte_index] & (1ul << bit_index)))
			return block;

		block++;
	}

	/* Now test several bits at a time */
	size_t ulong_index = ((block_count + block - 1) >> 3) / sizeof(unsigned long);
	free_list = free_list + ulong_index;
	while (block + ulong_bits < block_count) {
		if (*free_list != ULONG_MAX)
			return block + __builtin_ctzl(~(*free_list));

		block += ulong_bits;
		free_list++;
	}

	/* Now test the rest of the bits 1 at a time */
	while (block < block_count) {
		size_t byte_index = (block_count + block - 1) >> 3;
		unsigned int bit_index = (block_count + block - 1) & 7;

		if ((free_list8[byte_index] & (1ul << bit_index)) == 0)
			return block;

		block++;
	}

	return ULONG_MAX;
}

static inline void __alloc_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
	((u8*)free_list)[byte_index] |= (1 << bit_index);
}

static inline void __free_block(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
	((u8*)free_list)[byte_index] &= ~(1 << bit_index);
}

static inline bool __is_block_free(unsigned long* free_list, unsigned long block_count, unsigned long block) {
	size_t byte_index = (block_count + block - 1) >> 3;
	unsigned int bit_index = (block_count + block - 1) & 7;
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
	atomic_sub_fetch_explicit(&area->free_blocks[layer], 1, ATOMIC_RELAXED);

	unsigned long tmp = block;
	unsigned int tmp2 = layer;

	/* Allocate the bigger blocks */
	while (layer--) {
		block_count = 1ul << layer;
		block >>= 1;
		if (__is_block_free(area->pages.free_list, block_count, block)) {
			__alloc_block(area->pages.free_list, block_count, block);
			atomic_sub_fetch_explicit(&area->free_blocks[layer], 1, ATOMIC_RELAXED);
		}
	}

	block = tmp;
	layer = tmp2;

	/* Allocate the smaller blocks below the layer we allocated on */
	unsigned long times = 2;
	while (++layer < area->layer_count) {
		block_count = 1ul << layer;
		block <<= 1;
		for (unsigned long i = 0; i < times; i++) {
			bug(!__is_block_free(area->pages.free_list, block_count, block + i));
			__alloc_block(area->pages.free_list, block_count, block + i);
			atomic_sub_fetch_explicit(&area->free_blocks[layer], 1, ATOMIC_RELAXED);
		}
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
		atomic_add_fetch_explicit(&area->free_blocks[layer], 1, ATOMIC_RELAXED);

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
		for (unsigned long i = 0; i < count; i++) {
			bug(__is_block_free(area->pages.free_list, block_count, block + i));
			__free_block(area->pages.free_list, block_count, block + i);
			atomic_add_fetch_explicit(&area->free_blocks[layer], 1, ATOMIC_RELAXED);
		}
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
 * Selects a memory area to allocate from
 *
 * This function will allocate the block of memory, and store the information about that block
 * into layer and block. The atomic parameter will determine whether or not the allocation can sleep,
 * and will always return an area that is marked as atomic. The IRQ flags are used for locking if the
 * allocation cannot sleep.
 *
 * Acquires area->lock, and increments area->refcnt
 */
static struct mem_area* select_mem_area(struct zone* zone, unsigned int order,
		unsigned int* layer, unsigned long* block, bool atomic, irqflags_t* irq_flags) {
	const int max_retries = 3;

	int retries = 0;
	unsigned long i = 0;
	while (retries < max_retries) {
		struct mem_area* best = NULL;
		unsigned int l;
		for (; i < zone->area_count; i++) {
			struct mem_area* a = &zone->areas[i];
			if (a->pages.atomic != atomic)
				continue;
			if (unlikely(a->layer_count <= order))
				continue;

			l = a->layer_count - order - 1;
			if (!atomic_load_explicit(&a->free_blocks[l], ATOMIC_RELAXED))
				continue;

			unsigned int crefs = atomic_load(&a->alloc_refcnt);
			if (crefs == 0) {
				best = a;
				break;
			} else if (retries == 0) {
				if (!best || crefs < atomic_load(&best->alloc_refcnt))
					best = a;
			} else {
				best = a;
				break;
			}
		}

		if (likely(best)) {
			atomic_add_fetch(&best->alloc_refcnt, 1);
			mem_area_lock(best, irq_flags);
			if (atomic_load_explicit(&best->free_blocks[l], ATOMIC_RELAXED)) {
				*layer = l;
				*block = find_first_free(best->pages.free_list, l);
				bug(*block == ULONG_MAX); /* This means the accounting logic is fucked up if this triggers */
				return best;
			}
			mem_area_unlock(best, irq_flags);
			atomic_sub_fetch(&best->alloc_refcnt, 1);
		} else {
			i = 0;
			retries++;
		}
	}

	return NULL;
}

/* Get a memory area based on an address. */
static struct mem_area* get_mem_area(struct zone* zone, physaddr_t addr) {
	unsigned long low = 0;
	unsigned long high = zone->area_count;

	while (low < high) {
		unsigned long mid = low + ((high - low) >> 1);
		struct mem_area* area = &zone->areas[mid];
		physaddr_t area_end = area->base + (area->total_blocks << PAGE_SHIFT);
		if (addr < area->base)
			high = mid;
		else if (addr >= area_end)
			low = mid + 1;
		else
			return area;
	}

	return NULL;
}

/*
 * Allocate pages from a memory zone, this function makes sure the memory
 * region is actually system RAM before returning the address.
 */
static physaddr_t __alloc_pages(struct zone* zone, mm_t mm_flags, unsigned int order) {
	size_t alloc_size = PAGE_SIZE << order;

	physaddr_t ret = 0;
	int err;

	unsigned int layer;
	unsigned long block;
	irqflags_t irq_flags;

	bool atomic = !!(mm_flags & MM_ATOMIC);
	struct mem_area* area = select_mem_area(zone, order, &layer, &block, atomic, &irq_flags);
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
			mem_area_unlock(area, &irq_flags);
			area = select_mem_area(zone, order, &layer, &block, atomic, &irq_flags);
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

	/*
	 * Now make sure this region is actually marked usable in the memory map.
	 * These regions are reserved at boot, but this serves as a sanity check
	 */
	if (unlikely(!mmap_region_check(ret, alloc_size))) {
		/* Go through each page, and see what usable pages there are, and free those blocks */
		for (size_t i = 0; i < alloc_size; i += PAGE_SIZE) {
			if (mmap_region_check(ret + i, PAGE_SIZE)) {
				block = ((ret + i) - area->base) >> PAGE_SHIFT;
				_free_block(area, area->layer_count - 1, block);
			}
		}

		block = ULONG_MAX;
		goto retry;
	}
out:
	if (area) {
		mem_area_unlock(area, &irq_flags);
		atomic_sub_fetch(&area->alloc_refcnt, 1);
	}
	return ret;
}

/*
 * Horribly inefficient, but i just want this to work. This shouldn't really run outside of really boot,
 * but in case it does for some reason, do locking
 */
static int __reserve_pages(struct zone* zone, physaddr_t addr, size_t size) {
	if (unlikely(size >= SIZE_MAX - PAGE_SIZE))
		return -ERANGE;

	size = ROUND_UP(size, PAGE_SIZE);
	for (size = ROUND_UP(size, PAGE_SIZE); size; addr += PAGE_SIZE, size -= PAGE_SIZE) {
		struct mem_area* area = get_mem_area(zone, addr);
		if (unlikely(!area))
			continue;

		irqflags_t irq_flags;
		const unsigned int layer = area->layer_count - 1;
		unsigned long block = (addr - area->base) >> PAGE_SHIFT;

		mem_area_lock(area, &irq_flags);
		int err = _alloc_block(area, layer, block);
		mem_area_unlock(area, &irq_flags);
		if (err && err != -EALREADY) /* Can happen for things like overlapping reserved regions */
			printk(PRINTK_WARN "mm: %#16lx error %i\n", addr, err);
	}

	return 0;
}

/* Free pages from a specific memory zone. */
static int __free_pages(struct zone* zone, physaddr_t addr, unsigned int order) {
	struct mem_area* area = get_mem_area(zone, addr);
	if (!area)
		return -EFAULT;

	unsigned int layer = area->layer_count - order - 1;
	size_t alloc_size = PAGE_SIZE << order;
	unsigned long block = (addr - area->base) / alloc_size;

	irqflags_t irq_flags;
	mem_area_lock(area, &irq_flags);
	int ret = _free_block(area, layer, block);
	mem_area_unlock(area, &irq_flags);
	return ret;
}

#define DMA_SIZE 0x1000000
#define DMA_AREA_COUNT ((DMA_SIZE >> MAX_ORDER) >> PAGE_SHIFT)
#define MAX_AREA_BMP_SIZE (((1 << (MAX_ORDER + 1)) >> 3) + 1)

/* Sanity check */
#if (DMA_SIZE >> MAX_ORDER) < PAGE_SIZE
#error "MAX order too large for DMA_SIZE"
#endif /* (DMA_SIZE >> MAX_ORDER) < PAGE_SIZE */

static struct mem_area dma_areas[DMA_AREA_COUNT];
static unsigned long dma_area_free_lists[DMA_AREA_COUNT][MAX_AREA_BMP_SIZE / sizeof(unsigned long)] = { 0 };
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
 * Get a memory zone from MM flags, this function must select
 * the most restrictive zone if multiple zones are set for whatever reason.
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

/*
 * Checks if an address range is in a memory zone, this function does assume
 * that all memory areas are ordered.
 */
static inline bool in_zone(struct zone* zone, physaddr_t base, physaddr_t top) {
	struct mem_area* first_area = &zone->areas[0];
	struct mem_area* last_area = &zone->areas[zone->area_count - 1];
	return (base >= first_area->base && top <= last_area->base + last_area->real_size);
}

/*
 * Gets a memory zone from a physical memory address, this function
 * returns NULL if the address range falls inside of two different zones
 */
static struct zone* get_zone_addr(physaddr_t addr, size_t size) {
	physaddr_t addr_top;
	if (__builtin_add_overflow(addr, size, &addr_top))
		return NULL;

	if (in_zone(&dma_zone, addr, addr_top))
		return &dma_zone;
	if (in_zone(dma32_zone, addr, addr_top))
		return dma32_zone;
	if (in_zone(normal_zone, addr, addr_top))
		return normal_zone;

	return NULL;
}

static atomic(u64) mem_in_use = atomic_init(0);
static u64 mem_total = 0;

u64 get_free_memory(u64* total) {
	*total = mem_total;
	return atomic_load(&mem_in_use);
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
	const unsigned int max_retries = mm_flags & MM_ATOMIC ? 0 : 10;
	unsigned int retries = max_retries;
	do {
		physaddr_t physical = __alloc_pages(zone, mm_flags, order);
		if (physical) {
			atomic_add_fetch(&mem_in_use, (1u << order) << PAGE_SHIFT);
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

	atomic_sub_fetch(&mem_in_use, (1u << order) << PAGE_SHIFT);
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

static void alloc_rounded_area(struct mem_area* area) {
	unsigned long start_block = area->real_size >> PAGE_SHIFT;
	unsigned long end_block = area->size >> PAGE_SHIFT;
	for (unsigned long block = start_block; block < end_block; block++)
		bug(_alloc_block(area, area->layer_count - 1, block) != 0);
}

/* All structures for this zone are statically allocated */
static void dma_zone_init(physaddr_t last_usable) {
	dma_zone.zone_type = MM_ZONE_DMA;
	dma_zone.areas = dma_areas;

	size_t rest = unlikely(last_usable < DMA_SIZE) ? last_usable : DMA_SIZE;
	for (dma_zone.area_count = 0; dma_zone.area_count < DMA_AREA_COUNT && rest; dma_zone.area_count++) {
		struct mem_area* area = &dma_areas[dma_zone.area_count];

		const size_t max_area_size = (1u << MAX_ORDER) * PAGE_SIZE;
		area->base = dma_zone.area_count * max_area_size;
		area->pages.free_list = dma_area_free_lists[dma_zone.area_count];
		area->size = max_area_size;
		area->real_size = rest < max_area_size ? rest : max_area_size;
		area->layer_count = get_layer_count(max_area_size);
		area->total_blocks = 1u << (area->layer_count - 1);
		atomic_store_explicit(&area->alloc_refcnt, 0, ATOMIC_RELAXED);
		for (unsigned int layer = 0; layer < area->layer_count; layer++)
			atomic_store_explicit(&area->free_blocks[layer], 1ul << layer, ATOMIC_RELAXED);

		const unsigned long atomic_count = 1;
		area->pages.atomic = dma_zone.area_count < atomic_count;
		if (area->pages.atomic)
			spinlock_init(&area->pages.spinlock);
		else
			mutex_init(&area->pages.mutex);

		if (area->size != area->real_size)
			alloc_rounded_area(area);

		rest -= area->real_size;
	}

	/* Allocate the first page of memory, an error should never happen in this context */
	bug(_alloc_block(&dma_areas[0], dma_areas[0].layer_count - 1, 0) != 0);
}

/* 
 * Initialize a memory area, free_list_zone is the zone the free list should be allocated from,
 * so this function can be used when not every zone is initialized yet.
 */
static int init_area(struct mem_area* area, physaddr_t base, size_t real_size, bool atomic, struct zone* alloc_zone) {
	u64 rounded_size = round_power2(PAGE_SIZE, real_size);
	unsigned int layer_count = get_layer_count(rounded_size);
	if (layer_count == 1)
		return -ELOOP;

	size_t free_list_size = ((1 << layer_count) >> 3) + 1;
	unsigned int order = get_order(free_list_size);
	physaddr_t free_list = __alloc_pages(alloc_zone, 0, order);
	if (!free_list) {
		free_list = __alloc_pages(alloc_zone, MM_ATOMIC, order);
		if (!free_list)
			return -ENOMEM;
	}

	area->base = base;
	area->real_size = real_size;
	area->size = rounded_size;
	area->layer_count = layer_count;
	area->total_blocks = 1 << (layer_count - 1);
	for (unsigned long layer = 0; layer < area->layer_count; layer++)
		atomic_store_explicit(&area->free_blocks[layer], 1ul << layer, ATOMIC_RELAXED);
	area->pages.free_list = hhdm_virtual(free_list);
	area->pages.atomic = atomic;
	if (atomic)
		spinlock_init(&area->pages.spinlock);
	else
		mutex_init(&area->pages.mutex);
	memset(area->pages.free_list, 0, free_list_size);
	atomic_store_explicit(&area->alloc_refcnt, 0, ATOMIC_RELAXED);

	if (rounded_size != real_size)
		alloc_rounded_area(area);
	return 0;
}

/* Initialize a memory zone, alloc_zone is the zone to allocate the memory structures from */
static int zone_init(struct zone* zone, mm_t zone_type,
		physaddr_t last_usable, physaddr_t min_start, physaddr_t max_end,
		struct zone* alloc_zone) {
	if (last_usable < min_start)
		return -ELOOP;
	if (last_usable < max_end)
		max_end = last_usable;

	struct zone* const original_alloc_zone = alloc_zone;

	/* First allocate the array of areas for the zone */
	size_t zone_size = max_end - min_start;
	const size_t max_area_size = PAGE_SIZE << (MAX_ORDER);
	unsigned long area_count = zone_size / max_area_size;
	if (unlikely(area_count == 0))
		area_count = 1;
	unsigned long atomic_count = (area_count * 5 + 99) / 100;
	if (atomic_count == 0)
		atomic_count = 1;
	unsigned int area_order = get_order(sizeof(struct mem_area) * area_count);
	physaddr_t _areas = __alloc_pages(original_alloc_zone, 0, area_order);
	if (unlikely(!_areas)) {
		_areas = __alloc_pages(original_alloc_zone, MM_ATOMIC, area_order);
		if (unlikely(!_areas))
			return -ENOMEM;
	}

	struct mem_area* areas = hhdm_virtual(_areas);
	size_t rest = zone_size;
	zone->zone_type = zone_type;
	zone->areas = areas;
	for (zone->area_count = 0; zone->area_count < area_count; zone->area_count++) {
		size_t area_size = rest > max_area_size ? max_area_size : rest;
		int err = init_area(&areas[zone->area_count], min_start, area_size, zone->area_count < atomic_count, alloc_zone);

		if (likely(err == 0)) {
			min_start += area_size;
			rest -= area_size;
			alloc_zone = zone; /* At least one area, so an attempt can be made at using the current zone */
			continue;
		} else if (unlikely(err == -ELOOP)) {
			if (unlikely(--area_count == 0)) /* Don't bother with the zone */
				__free_pages(original_alloc_zone, _areas, area_order);
			else
				break; /* Able to create at least one zone, so it's still usable */
		} else if (err == -ENOMEM && zone == alloc_zone) {
			alloc_zone = original_alloc_zone; /* Swap back to the original zone, and try the allocation there */
			continue;
		}

		return err;
	}

	return 0;
}

#define DMA32_START 0x1000000
#define DMA32_END 0x100000000
#define NORMAL_START 0x100000000
#define NORMAL_END PHYSADDR_MAX

static inline void reserve_pages(physaddr_t addr, size_t size) {
	int errdma = __reserve_pages(&dma_zone, addr, size);
	int errdma32 = __reserve_pages(dma32_zone, addr, size);
	int errnormal = __reserve_pages(normal_zone, addr, size);
	if (unlikely(errdma || errdma32 || errnormal)) {
		printk(PRINTK_ERR "mm: Failed to reserve region %#16lx: dma: %i, dma32: %i, normal: %i\n",
				addr, errdma, errdma32, errnormal);
	}
}

static void reserve_unusable_memory(void) {
	const struct limine_mmap_response* mmap = mmap_request.response;
	for (u64 i = 0; i < mmap->entry_count; i++) {
		const struct limine_mmap_entry* entry = mmap->entries[i];
		if (entry->type != LIMINE_MMAP_USABLE)
			reserve_pages(entry->base, entry->length);
	}
}

void buddy_init(void) {
	mem_total = mmap_total_usable();

	physaddr_t last_usable = mmap_get_last_usable();
	dma_zone_init(last_usable);

	int err = zone_init(&__dma32_zone, MM_ZONE_DMA32, last_usable, DMA32_START, DMA32_END, &dma_zone);
	if (unlikely(err)) {
		if (unlikely(err == -ELOOP)) {
			dma32_zone = &dma_zone;
			normal_zone = &dma_zone;
			goto out;
		}
		panic("Failed to initialize zone DMA32: %i", err);
	}
	dma32_zone = &__dma32_zone;

	err = zone_init(&__normal_zone, MM_ZONE_NORMAL, last_usable, NORMAL_START, NORMAL_END, dma32_zone);
	if (err) {
		if (likely(err == -ELOOP)) {
			normal_zone = dma32_zone;
			goto out;
		}
		panic("Failed to initialize zone normal: %i", err);
	}
	normal_zone = &__normal_zone;

out:
	if (unlikely(dma32_zone == &dma_zone))
		printk(PRINTK_DBG "mm: DMA32 linked to DMA\n");
	else if (dma32_zone == normal_zone)
		printk(PRINTK_DBG "mm: Normal linked to DMA32\n");
	reserve_unusable_memory();
}
