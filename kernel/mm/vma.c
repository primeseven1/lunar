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
	u8* old_free_list = vma->free_list;
	size_t old_free_list_size = (vma->page_count + 7) / 8;

	size_t new_free_list_size = (new_page_count + 7) / 8;
	physaddr_t _new_free_list = alloc_pages(MM_ZONE_NORMAL, get_order(new_free_list_size));
	if (!_new_free_list)
		return -ENOMEM;
	u8* new_free_list = hhdm_virtual(_new_free_list);

	if (new_page_count > vma->page_count) {
		memset(new_free_list, 0, new_free_list_size);
		memcpy(new_free_list, old_free_list, old_free_list_size);
	} else {
		memcpy(new_free_list, old_free_list, new_free_list_size);
	}

	if (old_free_list)
		free_pages(hhdm_physical(old_free_list), get_order(old_free_list_size));
	vma->free_list = new_free_list;
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
		if (vma->free_list[byte_index] & (1 << bit_index))
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
		if (align && consecutive_free == 0 && ((uintptr_t)vma->start + i * PAGE_SIZE) & (align - 1))
			continue;

		size_t byte_index = i / 8;
		unsigned int bit_index = i % 8;

		if (!(vma->free_list[byte_index] & (1 << bit_index))) {
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
		for (unsigned long i = start_index; i < start_index + count; i++) {
			size_t byte_index = i / 8;
			unsigned int bit_index = i % 8;
			vma->free_list[byte_index] |= (1 << bit_index);
		}

		return (u8*)vma->start + start_index * PAGE_SIZE;
	}

	return NULL;
}

static void __vma_free_pages(struct vma* vma, void* addr, unsigned long count) {
	unsigned long index = ((u8*)addr - (u8*)vma->start) >> PAGE_SHIFT;
	for (unsigned long i = index; i < index + count; i++) {
		size_t byte_index = i / 8;
		unsigned int bit_index = i % 8;
		vma->free_list[byte_index] &= ~(1 << bit_index);
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

	unsigned long flags;
	spinlock_lock_irq_save(&vma->lock, &flags);

	if (addr < vma->start || (u8*)addr + size >= (u8*)vma->end) {
		printk(PRINTK_ERR "mm: Failed to free virtual address, address out of range of zone\n");
		dump_stack();
		goto out;
	}

	__vma_free_pages(vma, addr, page_count);
out:
	spinlock_unlock_irq_restore(&vma->lock, &flags);
}

struct vma* vma_create(void* start, void* end, unsigned long page_count) {
	if (start >= end)
		return NULL;
	physaddr_t _vma = alloc_pages(MM_ZONE_NORMAL, get_order(sizeof(struct vma)));
	if (!_vma)
		return NULL;
	struct vma* vma = hhdm_virtual(_vma);

	u8* free_list = NULL;
	if (page_count != 0) {
		size_t free_list_size = (page_count + 7) / 8;
		physaddr_t _free_list = alloc_pages(MM_ZONE_NORMAL, get_order(free_list_size));
		if (!_free_list) {
			free_pages(_vma, get_order(sizeof(struct vma)));
			return NULL;
		}

		free_list = hhdm_virtual(_free_list);
		memset(free_list, 0, free_list_size);
	}

	vma->start = start;
	vma->end = end;
	vma->free_list = free_list;
	vma->page_count = page_count;
	atomic_store(&vma->lock, SPINLOCK_INITIALIZER, ATOMIC_RELAXED);

	return vma;
}

int vma_destroy(struct vma* vma) {
	size_t free_list_size = (vma->page_count + 7) / 8;
	if (!are_last_n_pages_free(vma, vma->page_count))
		return -EEXIST;

	free_pages(hhdm_physical(vma->free_list), get_order(free_list_size));
	free_pages(hhdm_physical(vma), get_order(sizeof(struct vma)));

	return 0;
}
