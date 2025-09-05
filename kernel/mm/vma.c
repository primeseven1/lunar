#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/errno.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/trace.h>
#include <crescent/lib/string.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vma.h>
#include "hhdm.h"

static struct vma* vma_alloc(void) {
	physaddr_t _vma = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(struct vma)));
	struct vma* vma = hhdm_virtual(_vma);
	list_node_init(&vma->link);
	return vma;
}

static void vma_free(struct vma* vma) {
	free_pages(hhdm_physical(vma), get_order(sizeof(*vma)));
}

struct vma* vma_find(struct mm* mm, const void* address) {
	struct list_node* pos;
	list_for_each(pos, &mm->vma_list) {
		struct vma* vma = list_entry(pos, struct vma, link);
		if ((uintptr_t)address >= vma->start && (uintptr_t)address < vma->top)
			return vma;
	}
	return NULL;
}

static void vma_rip(struct mm* mm, void* start, size_t size) {
	assert((uintptr_t)start % PAGE_SIZE == 0);

	unsigned long count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (unsigned long i = 0; i < count; i++, start = (u8*)start + PAGE_SIZE) {
		struct vma* vma = vma_find(mm, start);
		if (!vma)
			continue;
		assert(vma_unmap(mm, start, PAGE_SIZE) == 0);
	}
}

int vma_map(struct mm* mm, void* hint, size_t size, mmuflags_t prot, int flags, void** ret) {
	size_t align = PAGE_SIZE;
	if (flags & VMM_HUGEPAGE_2M)
		align = HUGEPAGE_2M_SIZE;

	if (size == 0 || ((!hint || (uintptr_t)hint % align) && flags & VMM_FIXED))
		return -EINVAL;

	if (size >= SIZE_MAX - align)
		return -ERANGE;
	size = ROUND_UP(size, align);

	struct vma* vma = vma_alloc();
	vma->prot = prot;
	vma->flags = flags;

	uintptr_t base = (uintptr_t)hint;
	if (base >= SIZE_MAX - align) {
		vma_free(vma);
		return -ERANGE;
	}
	base = ROUND_UP(base, align);
	if (!(flags & VMM_FIXED) && (base < (uintptr_t)mm->mmap_start || base + size > (uintptr_t)mm->mmap_end))
		base = (uintptr_t)mm->mmap_start;
	if (flags & VMM_FIXED && !(flags & VMM_NOREPLACE)) {
		if (__builtin_add_overflow(base, size, &vma->top)) {
			vma_free(vma);
			return -ERANGE;
		}
		vma_rip(mm, hint, size);
	}

	/* Skip VMA's that end at or before the hint */
	struct vma* prev = NULL;
	struct vma* iter;
	list_for_each_entry(iter, &mm->vma_list, link) {
		if (iter->top > base)
			break;
		prev = iter;
	}

	/* Find a memory hole large enough for the size */
	uintptr_t addr = base;
	list_for_each_entry_cont(iter, &mm->vma_list, link) {
		if (flags & VMM_HUGEPAGE_2M) {
			if (iter->start - addr >= size + HUGEPAGE_2M_SIZE)
				break;
		} else if (iter->start - addr >= size) {
			break;
		}

		addr = iter->top;
		prev = iter;
	}

	if (flags & VMM_FIXED && addr != (uintptr_t)hint) {
		vma_free(vma);
		return -EEXIST;
	} else if (addr >= (uintptr_t)mm->mmap_end) {
		vma_free(vma);
		return -ENOMEM;
	}

	if (flags & VMM_HUGEPAGE_2M)
		addr = ROUND_UP(addr, HUGEPAGE_2M_SIZE);

	vma->start = addr;
	if (__builtin_add_overflow(addr, size, &vma->top)) {
		vma_free(vma);
		return -ERANGE;
	}

	if (likely(prev))
		list_add_between(&prev->link, &iter->link, &vma->link);
	else
		list_add(&mm->vma_list, &vma->link);

	*ret = (void*)vma->start;
	return 0;
}

int vma_protect(struct mm* mm, void* address, size_t size, mmuflags_t prot) {
	if (!address || size == 0 || (uintptr_t)address % PAGE_SIZE)
		return -EINVAL;

	struct vma* start_split = vma_alloc();
	struct vma* end_split = vma_alloc();

	bool start_split_needed = false;
	bool end_split_needed = false;

	int err = 0;

	uintptr_t start = (uintptr_t)address;
	uintptr_t end;
	if (__builtin_add_overflow(start, size, &end)) {
		err = -ERANGE;
		goto out;
	}
	if (end >= UINTPTR_MAX - PAGE_SIZE) {
		err = -ERANGE;
		goto out;
	}
	end = ROUND_UP(end, PAGE_SIZE);

	struct vma* v = NULL;
	struct vma* pos;
	list_for_each_entry(pos, &mm->vma_list, link) {
		if (pos->top > start) {
			v = pos;
			break;
		}
	}
	if (!v) {
		err = -ENOENT;
		goto out;
	}

	/* Handle start split */
	if (start > v->start) {
		start_split_needed = true;
		start_split->start = start;
		start_split->top = v->top;
		start_split->prot = v->prot;
		start_split->flags = v->flags;
		v->top = start;
		list_add_between(&v->link, v->link.next, &start_split->link);
	}

	/* Handle end split */
	struct vma* u = NULL;
	list_for_each_entry_cont(pos, &mm->vma_list, link) {
		if (pos->top >= end) {
			u = pos;
			break;
		}
	}
	assert(u != NULL);
	if (end < u->top) {
		end_split_needed = true;
		end_split->start = end;
		end_split->top = u->top;
		end_split->prot = u->prot;
		end_split->flags = u->flags;
		u->top = end;
		list_add_between(&u->link, u->link.next, &end_split->link);
	}

	/* Apply protection flags */
	struct vma* adj;
	list_for_each_entry(adj, &mm->vma_list, link) {
		if (adj->start >= start && adj->start < end)
			adj->prot = prot;
	}

	/* Merge adjecent VMA's with the same protection flags */
	struct vma* current = list_first_entry(&mm->vma_list, struct vma, link);
	while (!list_is_last(&mm->vma_list, &current->link)) {
		struct vma* next = list_next_entry(current, link);
		if (current->top == next->start && current->prot == next->prot && current->flags == next->flags) {
			current->top = next->top;
			list_remove(&next->link);
			vma_free(next);
			continue;
		}

		current = next;
	}
out:
	if (!start_split_needed)
		vma_free(start_split);
	if (!end_split_needed)
		vma_free(end_split);
	return err;
}

int vma_unmap(struct mm* mm, void* address, size_t size) {
	if (size == 0 || !address || (uintptr_t)address % PAGE_SIZE)
		return -EINVAL;

	struct vma* split = vma_alloc();
	bool split_needed = false;
	bool overlap_found = false;

	int err = 0;

	uintptr_t start = (uintptr_t)address;
	uintptr_t end;
	if (__builtin_add_overflow(start, size, &end)) {
		err = -ERANGE;
		goto out;
	}
	if (end >= UINTPTR_MAX - PAGE_SIZE) {
		err = -ERANGE;
		goto out;
	}

	struct vma* v, *n;
	list_for_each_entry_safe(v, n, &mm->vma_list, link) {
		if (v->top <= start || v->start >= end)
			continue;

		overlap_found = true;

		/* Handle full overlap, head chop, and tail chop, and middle splitting respectively */
		if (start <= v->start && end >= v->top) {
			list_remove(&v->link);
			vma_free(v);
		} else if (start <= v->start) {
			v->start = end;
			goto out;
		} else if (end >= v->top) {
			v->top = start;
		} else {
			split_needed = true;
			split->start = end;
			split->flags = v->flags;
			split->prot = v->prot;
			split->top = v->top;
			v->top = start;
			list_add_between(&v->link, v->link.next, &split->link);
			goto out;
		}
	}

	if (!overlap_found)
		err = -ENOENT;
out:
	if (!split_needed)
		vma_free(split);
	return err;
}
