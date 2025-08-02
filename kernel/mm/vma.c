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

static struct vma* vma_alloc(void) {
	physaddr_t vma = alloc_pages(MM_ZONE_NORMAL, get_order(sizeof(struct vma)));
	if (!vma)
		return NULL;
	return hhdm_virtual(vma);
}

static void vma_free(struct vma* vma) {
	free_pages(hhdm_physical(vma), get_order(sizeof(*vma)));
}

struct vma* vma_find(struct mm* mm, const void* address) {
	for (struct vma* vma = mm->vma_list; vma; vma = vma->next) {
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
	if (!vma)
		return -ENOMEM;
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
	struct vma* iter = mm->vma_list;
	while (iter && iter->top <= base) {
		prev = iter;
		iter = iter->next;
	}

	/* Find a memory hole large enough for the size */
	uintptr_t addr = base;
	while (iter) {
		if (flags & VMM_HUGEPAGE_2M) {
			if (iter->start - addr >= size + HUGEPAGE_2M_SIZE)
				break;
		} else if (iter->start - addr >= size) {
			break;
		}

		addr = iter->top;
		prev = iter;
		iter = iter->next;
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

	vma->prev = prev;
	vma->next = iter;
	if (prev)
		prev->next = vma;
	else
		mm->vma_list = vma;
	if (iter)
		iter->prev = vma;

	*ret = (void*)vma->start;
	return 0;
}

int vma_protect(struct mm* mm, void* address, size_t size, mmuflags_t prot) {
	if (!address || size == 0 || (uintptr_t)address % PAGE_SIZE)
		return -EINVAL;

	/* 
	 * Prepare before attempting to do surgery on the VMA's, so we don't have to attempt to 
	 * reverse the changes and have the VMA's blow up if done wrong.
	 */
	struct vma* start_split = vma_alloc();
	if (!start_split)
		return -ENOMEM;
	struct vma* end_split = vma_alloc();
	if (!end_split) {
		vma_free(start_split);
		return -ENOMEM;
	}

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

	struct vma* v = mm->vma_list;
	while (v && v->top <= start)
		v = v->next;
	if (!v || v->start >= end) {
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
		start_split->prev = v;
		start_split->next = v->next;
		if (v->next)
			v->next->prev = start_split;
		v->next = start_split;
		v->top = start;
		v = start_split;
	}

	/* Handle end split */
	struct vma* u = v;
	while (u->top < end)
		u = u->next;
	if (end < u->top) {
		end_split_needed = true;
		end_split->start = end;
		end_split->top = u->top;
		end_split->prot = u->prot;
		end_split->flags = u->flags;
		end_split->prev = u;
		end_split->next = u->next;
		u->top = end;
		if (u->next)
			u->next->prev = end_split;
		u->next = end_split;
	}

	/* Apply protection flags */
	for (struct vma* adj = v; adj && adj->start < end; adj = adj->next)
		adj->prot = prot;

	/* Merge adjecent VMA's with the same protection flags */
	for (struct vma* adj = mm->vma_list; adj && adj->next;) {
		struct vma* n = adj->next;
		if (adj->top == n->start && adj->prot == n->prot && adj->flags == n->flags) {
			adj->top = n->top;
			adj->next = n->next;
			if (n->next)
				n->next->prev = adj;
			vma_free(n);
		} else {
			adj = n;
		}
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

	/* Prepare for a potential split before doing surgery on the VMA's */
	struct vma* split = vma_alloc();
	if (!split)
		return -ENOMEM;
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

	struct vma* v = mm->vma_list;
	while (v) {
		if (v->top <= start || v->start >= end) {
			v = v->next;
			continue;
		}

		overlap_found = true;

		/* Handle full overlap, head chop, and tail chop, and middle splitting respectively */
		if (start <= v->start && end >= v->top) {
			struct vma* free = v;
			v = v->next;
			if (free->prev)
				free->prev->next = free->next;
			else
				mm->vma_list = free->next;
			if (free->next)
				free->next->prev = free->prev;
			vma_free(free);
		} else if (start <= v->start) {
			v->start = end;
			goto out;
		} else if (end >= v->top) {
			v->top = start;
			v = v->next;
		} else {
			split_needed = true;
			split->start = end;
			split->flags = v->flags;
			split->prot = v->prot;
			split->top = v->top;
			split->next = v->next;
			split->prev = v;
			if (v->next)
				v->next->prev = split;
			v->next = split;
			v->top = start;
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
