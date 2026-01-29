#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/asm/errno.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/trace.h>
#include <lunar/lib/string.h>
#include <lunar/mm/slab.h>
#include <lunar/mm/vma.h>
#include <lunar/mm/vmm.h>
#include <lunar/mm/hhdm.h>

static struct slab_cache* vma_cache = NULL;

static void vma_ctor(void* obj) {
	struct vma* vma = obj;
	list_node_init(&vma->link);
}

static struct vma* vma_alloc(void) {
	if (unlikely(!vma_cache)) {
		vma_cache = slab_cache_create(sizeof(struct vma), _Alignof(struct vma),
				MM_ZONE_NORMAL | MM_NOFAIL, vma_ctor, NULL);
	}
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

static void vma_rip(struct mm* mm, uintptr_t start, size_t size) {
	size_t count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	for (size_t i = 0; i < count; i++, start = start + PAGE_SIZE) {
		struct vma* vma = vma_find(mm, start);
		if (!vma)
			continue;
		assert(vma_unmap(mm, start, PAGE_SIZE) == 0);
	}
}

int vma_map(struct mm* mm, uintptr_t hint, size_t size, mmuflags_t prot, int flags, uintptr_t* ret) {
	size_t align = PAGE_SIZE;
	if (flags & VMM_HUGEPAGE_2M)
		align = HUGEPAGE_2M_SIZE;

	if (size == 0 || ((!hint || hint % align) && flags & VMM_FIXED))
		return -EINVAL;

	if (size >= SIZE_MAX - align)
		return -ERANGE;
	size = ROUND_UP(size, align);

	struct vma* vma = vma_alloc();
	vma->prot = prot;
	vma->flags = flags;

	uintptr_t base = hint;
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

	if (flags & VMM_FIXED && addr != hint) {
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
		list_add_after(&prev->link, &vma->link);
	else
		list_add(&mm->vma_list, &vma->link);

	*ret = vma->start;
	return 0;
}

int vma_protect(struct mm* mm, uintptr_t address, size_t size, mmuflags_t prot) {
	if (!address || size == 0 || address % PAGE_SIZE)
		return -EINVAL;

	uintptr_t end;
	if (__builtin_add_overflow(address, size, &end))
		return -ERANGE;
	if (end >= UINTPTR_MAX - PAGE_SIZE)
		return -ERANGE;
	end = ROUND_UP(end, PAGE_SIZE);

	struct vma* v = NULL;
	struct vma* pos;
	list_for_each_entry(pos, &mm->vma_list, link) {
		if (pos->top > address) {
			v = pos;
			break;
		}
	}
	if (!v)
		return -ENOENT;

	/* Handle start split */
	if (address > v->start) {
		struct vma* start_split = vma_alloc();
		start_split->start = address;
		start_split->top = v->top;
		start_split->prot = v->prot;
		start_split->flags = v->flags;
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
		end_split->flags = u->flags;
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
		if (current->top == next->start && current->prot == next->prot && current->flags == next->flags) {
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
			split->flags = v->flags;
			split->prot = v->prot;
			split->top = v->top;
			v->top = address;
			list_add_after(&v->link, &split->link);
			break;
		}
	}

	return overlap_found ? 0 : -ENOENT;
}
