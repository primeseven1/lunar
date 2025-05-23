#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/errno.h>
#include <crescent/core/locking.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/trace.h>
#include <crescent/lib/string.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vma.h>
#include "hhdm.h"

static int vma_realloc(struct vma* vma, unsigned long new_page_count) {
	u8* old_map = vma->map;
	size_t old_map_size = (vma->page_count >> 3) + 1;

	size_t new_map_size = (new_page_count >> 3) + 1;
	physaddr_t _new_map = alloc_pages(MM_ZONE_NORMAL, get_order(new_map_size));
	if (!_new_map)
		return -ENOMEM;
	u8* new_map = hhdm_virtual(_new_map);

	if (new_page_count > vma->page_count) {
		memset(new_map, 0, new_map_size);
		memcpy(new_map, old_map, old_map_size);
	} else {
		memcpy(new_map, old_map, new_map_size);
	}

	free_pages(hhdm_physical(old_map), get_order(old_map_size));
	vma->map = new_map;
	vma->page_count = new_page_count;

	return 0;
}

static int vma_expand(struct vma* vma, unsigned long page_count) {
	if (page_count == 0)
		return 0;

	unsigned long new_page_count;
	if (unlikely(__builtin_add_overflow(vma->page_count, page_count, &new_page_count)))
		return -ERANGE;
	else if (unlikely((u8*)vma->start + (new_page_count * PAGE_SIZE) >= (u8*)vma->end))
		return -ERANGE;

	return vma_realloc(vma, new_page_count);
}

static bool are_last_n_pages_free(struct vma* vma, unsigned long n) {
	if (n < vma->page_count)
		return false;

	unsigned long start_index = vma->page_count - n;
	for (unsigned long i = start_index; i < vma->page_count; i++) {
		size_t byte_index = i >> 3;
		unsigned int bit_index = i & 7;
		if (vma->map[byte_index] & (1 << bit_index))
			return false;
	}

	return true;
}

static int vma_shrink(struct vma* vma, unsigned long page_count) {
	if (page_count == 0)
		return 0;

	unsigned long new_page_count;
	if (unlikely(__builtin_sub_overflow(vma->page_count, page_count, &new_page_count)))
		return -ERANGE;

	return vma_realloc(vma, new_page_count);
}

static void* __vma_alloc_pages_aligned(struct vma* vma, unsigned long count, size_t align) {
	unsigned long consecutive_free = 0;
	unsigned long start_index = 0;

	bool found = false;

	for (unsigned long i = 0; i < vma->page_count; i++) {
		if (align && consecutive_free == 0 && 
				((uintptr_t)vma->start + i * PAGE_SIZE) & (align - 1))
			continue;

		size_t byte_index = i >> 3;
		unsigned int bit_index = i & 7;

		if (!(vma->map[byte_index] & (1 << bit_index))) {
			if (consecutive_free == 0)
				start_index = i;

			consecutive_free++;
			if (consecutive_free == count) {
				found = true;
				break;
			}
		} else {
			consecutive_free = 0;
		}
	}

	if (found) {
		for (unsigned long i = 0; i < start_index + count; i++) {
			size_t byte_index = i >> 3;
			unsigned int bit_index = i & 7;
			vma->map[byte_index] |= (1 << bit_index);
		}

		return (u8*)vma->start + start_index * PAGE_SIZE;
	}

	return NULL;
}

static void __vma_free_pages(struct vma* vma, void* addr, unsigned long count) {
	unsigned long index = ((u8*)addr - (u8*)vma->start) >> PAGE_SHIFT;
	for (unsigned long i = index; i < index + count; i++) {
		size_t byte_index = i >> 3;
		unsigned int bit_index = i & 7;
		vma->map[byte_index] &= ~(1 << bit_index);
	}

	if (are_last_n_pages_free(vma, vma->page_count / 2 / 2))
		vma_shrink(vma, vma->page_count / 2 / 2);
}

void* vma_alloc_pages(struct vma* vma, unsigned long page_count, size_t align) {
	if (align == 0)
		align = PAGE_SIZE;
	else if (align & (align - 1))
		return NULL;

	unsigned long flags;
	spinlock_lock_irq_save(&vma->lock, &flags);

	void* ret;
	while (1) {
		ret = __vma_alloc_pages_aligned(vma, page_count, align);
		if (ret)
			break;

		int err = vma_expand(vma, (page_count * 3) / 2);
		if (!err)
			continue;
		err = vma_expand(vma, page_count);
		if (err)
			break;
	}

	spinlock_unlock_irq_restore(&vma->lock, &flags);
	return ret;
}

void vma_free_pages(struct vma* vma, void* addr, unsigned long page_count) {
	size_t size = PAGE_SIZE * page_count;

	if (addr < vma->start || (u8*)addr + size >= (u8*)vma->end) {
		printk(PRINTK_ERR "mm: Failed to free virtual address, address out of range of zone\n");
		dump_stack();
		return;
	}

	unsigned long flags;
	spinlock_lock_irq_save(&vma->lock, &flags);
	__vma_free_pages(vma, addr, page_count);
	spinlock_unlock_irq_restore(&vma->lock, &flags);
}

struct vma* vma_create(void* start, void* end, unsigned long page_count) {
	physaddr_t _vma = alloc_pages(MM_ZONE_NORMAL, get_order(sizeof(struct vma)));
	if (!_vma)
		return NULL;
	struct vma* vma = hhdm_virtual(_vma);

	size_t map_size = (page_count >> 3) + 1;

	physaddr_t _map = alloc_pages(MM_ZONE_NORMAL, get_order(map_size));
	if (!_map) {
		free_pages(_vma, get_order(sizeof(struct vma)));
		return NULL;
	}
	u8* map = hhdm_virtual(_map);
	memset(map, 0, map_size);

	vma->start = start;
	vma->end = end;
	vma->map = map;
	vma->page_count = page_count;
	vma->lock = SPINLOCK_INITIALIZER;

	return vma;
}

int vma_destroy(struct vma* vma) {
	size_t map_size = (vma->page_count >> 3) + 1;
	if (!are_last_n_pages_free(vma, vma->page_count))
		return -EEXIST;

	free_pages(hhdm_physical(vma->map), get_order(map_size));
	free_pages(hhdm_physical(vma), get_order(sizeof(struct vma)));
	return 0;
}
