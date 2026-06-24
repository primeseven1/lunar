#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/panic.h>
#include <lunar/init.h>
#include <lunar/vmm.h>
#include <lunar/slab.h>
#include "internal.h"

static struct slab_cache* vma_cache;

static void vma_ctor(void* obj) {
	struct vma* vma = obj;
	list_node_init(&vma->link);
}

static struct vma* vma_alloc(void) {
	return slab_cache_alloc(vma_cache);
}

static void vma_free(struct vma* vma) {
	slab_cache_free(vma_cache, vma);
}

struct vma* vma_find(struct mm* mm, uintptr_t address) {
	struct list_node* pos;
	list_for_each(pos, &mm->vma_list) {
		struct vma* vma = list_entry(pos, struct vma, link);
		if (address >= vma->start && address < vma->top)
			return vma;
	}
	return NULL;
}

static int vma_rip(struct mm* mm, uintptr_t start, size_t size) {
	size_t count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int err = 0;
	for (size_t i = 0; i < count; i++, start = start + PAGE_SIZE) {
		struct vma* vma = vma_find(mm, start);
		if (!vma)
			continue;
		err = vma_unmap(mm, start, PAGE_SIZE);
		if (err == -EPERM)
			break;
		bug(err != 0);
	}
	return err;
}

int vma_map(struct mm* mm, uintptr_t hint, size_t size, pgprot_t prot, int vmm_flags, uintptr_t* ret) {
	size_t align = PAGE_SIZE;
	if (vmm_flags & VMM_HUGETLB) {
		if (vmm_flags & VMM_HUGETLB_1GB || PMD_SIZE != 0x200000)
			return -ENOTSUP;
		vmm_flags |= VMM_HUGETLB_2MB;
		align = PMD_SIZE;
	}

	if (size == 0 || ((!hint || hint % align) && vmm_flags & VMM_FIXED))
		return -EINVAL;

	if (size >= SIZE_MAX - align)
		return -ERANGE;
	size = ROUND_UP(size, align);

	struct vma* vma = vma_alloc();
	vma->prot = prot;
	vma->vmm_flags = vmm_flags;

	uintptr_t base = hint;
	if (base >= SIZE_MAX - align) {
		vma_free(vma);
		return -ERANGE;
	}
	base = ROUND_UP(base, align);
	struct vmm_range* range = (vmm_flags & VMM_STACK) ? &mm->stack : &mm->mmap;
	if (!(vmm_flags & VMM_FIXED) && (base < range->start || base + size > range->end))
		base = range->start;
	if (vmm_flags & VMM_FIXED && !(vmm_flags & VMM_NOREPLACE)) {
		if (__builtin_add_overflow(base, size, &vma->top)) {
			vma_free(vma);
			return -ERANGE;
		}
		int err = vma_rip(mm, hint, size);
		if (err) {
			vma_free(vma);
			return err;
		}
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
		if (vmm_flags & VMM_HUGETLB) {
			if (iter->start - addr >= size + align)
				break;
		} else if (iter->start - addr >= size) {
			break;
		}

		addr = iter->top;
		prev = iter;
	}

	if (vmm_flags & VMM_FIXED && addr != hint) {
		vma_free(vma);
		return -EEXIST;
	} else if (addr >= range->end) {
		vma_free(vma);
		size_t range_size = range->end - range->start;
		if (range_size >= range->max_size)
			return -ENOMEM;
		uintptr_t x;
		if (range->grows_down) {
			if (__builtin_sub_overflow(range->start, size, &x))
				return -ENOMEM;
			range->start = x;
		} else {
			if (__builtin_add_overflow(range->end, size, &x))
				return -ENOMEM;
			range->end = x;
		}
		return -EAGAIN;
	}

	if (vmm_flags & VMM_HUGETLB)
		addr = ROUND_UP(addr, align);

	vma->start = addr;
	if (__builtin_add_overflow(addr, size, &vma->top)) {
		vma_free(vma);
		return -ERANGE;
	}

	if (likely(prev))
		list_add_after(&prev->link, &vma->link);
	else
		list_add(&mm->vma_list, &vma->link);

	*ret = vma->start;
	return 0;
}

int vma_protect(struct mm* mm, uintptr_t address, size_t size, pgprot_t prot) {
	if (!address || size == 0 || address % PAGE_SIZE)
		return -EINVAL;

	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;
	if (end >= UINTPTR_MAX - PAGE_SIZE)
		return -ERANGE;
	end = ROUND_UP(end, PAGE_SIZE);

	struct vma* pos;
	struct vma* v = NULL;
	list_for_each_entry(pos, &mm->vma_list, link) {
		if (pos->top <= address)
			continue;
		if (!v)
			v = pos;
		if (pos->start >= end)
			break;
		if (pos->vmm_flags & VMM_SEALED)
			return -EPERM;
	}
	if (!v)
		return -ENOENT;

	/* Handle start split */
	if (address > v->start) {
		struct vma* start_split = vma_alloc();
		start_split->start = address;
		start_split->top = v->top;
		start_split->prot = v->prot;
		start_split->vmm_flags = v->vmm_flags;
		v->top = address;
		list_add_after(&v->link, &start_split->link);
	}

	/* Handle end split */
	struct vma* u = NULL;
	list_for_each_entry_cont(pos, &mm->vma_list, link) {
		if (pos->top >= end) {
			u = pos;
			break;
		}
	}
	bug(u == NULL);
	if (end < u->top) {
		struct vma* end_split = vma_alloc();
		end_split->start = end;
		end_split->top = u->top;
		end_split->prot = u->prot;
		end_split->vmm_flags = u->vmm_flags;
		u->top = end;
		list_add_after(&u->link, &end_split->link);
	}

	/* Apply protection flags */
	struct vma* adj;
	list_for_each_entry(adj, &mm->vma_list, link) {
		if (adj->start >= address && adj->start < end)
			adj->prot = prot;
	}

	/* Merge adjecent VMA's with the same protection flags */
	struct vma* current = list_first_entry(&mm->vma_list, struct vma, link);
	while (!list_is_last(&mm->vma_list, &current->link)) {
		struct vma* next = list_next_entry(current, link);
		if (current->top == next->start && current->prot == next->prot && current->vmm_flags == next->vmm_flags) {
			current->top = next->top;
			list_remove(&next->link);
			vma_free(next);
			continue;
		}

		current = next;
	}

	return 0;
}

int vma_unmap(struct mm* mm, uintptr_t address, size_t size) {
	if (size == 0 || !address || address % PAGE_SIZE)
		return -EINVAL;

	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;
	if (end >= UINTPTR_MAX - PAGE_SIZE)
		return -ERANGE;
	end = ROUND_UP(end, PAGE_SIZE);

	bool overlap_found = false;
	struct vma* v, *n;
	list_for_each_entry_safe(v, n, &mm->vma_list, link) {
		if (v->top <= address || v->start >= end)
			continue;
		if (v->vmm_flags & VMM_SEALED)
			return -EPERM;

		overlap_found = true;

		/* Handle full overlap, head chop, and tail chop, and middle splitting respectively */
		if (address <= v->start && end >= v->top) {
			list_remove(&v->link);
			vma_free(v);
		} else if (address <= v->start) {
			v->start = end;
			break;
		} else if (end >= v->top) {
			v->top = address;
		} else {
			struct vma* split = vma_alloc();
			split->start = end;
			split->vmm_flags = v->vmm_flags;
			split->prot = v->prot;
			split->top = v->top;
			v->top = address;
			list_add_after(&v->link, &split->link);
			break;
		}
	}

	return overlap_found ? 0 : -ENOENT;
}

static void vma_init(void) {
	vma_cache = slab_cache_create(sizeof(struct vma), alignof(struct vma),
			MM_ZONE_NORMAL | MM_NOFAIL, vma_ctor, NULL);
}

INIT_TASK_DECLARE(zones_init_task);
INIT_TASK_DEFINE(vma_init_task, INIT_TASK_SCOPE_BSP, vma_init, &zones_init_task);
