#include <lunar/common.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/mm/slab.h>
#include <lunar/core/panic.h>
#include "internal.h"

static struct slab_cache* snapshot_cache = NULL;

struct page_snapshot* snapshot_pages(struct mm* mm_struct, uintptr_t virtual, size_t size) {
	if (size > UINTPTR_MAX - (uintptr_t)virtual)
		return ERR_PTR(-EINVAL);
	uintptr_t end = virtual + size;
	virtual = ROUND_DOWN(virtual, PAGE_SIZE);
	if (end > UINTPTR_MAX - (PAGE_SIZE - 1))
		return ERR_PTR(-EINVAL);
	end = ROUND_UP(end, PAGE_SIZE);
	if (end < virtual)
		return ERR_PTR(-EINVAL);
	size = end - virtual;

	struct page_snapshot* snapshots = NULL;
	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
		if (!vma) {
			virtual += PAGE_SIZE;
			continue;
		}

		uintptr_t range_end = vma->top < end ? vma->top : end;
		while (virtual < range_end) {
			size_t ps = PAGE_SIZE;
			if (vma->flags & VMM_HUGEPAGE_2M) {
				ps = HUGEPAGE_2M_SIZE;
				virtual = ROUND_DOWN(virtual, ps);
			}

			if (unlikely(!snapshot_cache)) {
				snapshot_cache = slab_cache_create(sizeof(struct page_snapshot), _Alignof(struct page_snapshot),
						MM_ZONE_NORMAL | MM_NOFAIL, NULL, NULL);
			}
			struct page_snapshot* s = slab_cache_alloc(snapshot_cache);
			s->start = virtual;
			s->physical = pagetable_get_physical(mm_struct->pagetable, virtual);
			s->page_size = ps;
			s->mmu_flags = vma->prot;
			s->vmm_flags = vma->flags;

			s->next = snapshots;
			snapshots = s;
			virtual += ps;
		}
	}

	return snapshots;
}

static inline void snapshots_free(struct page_snapshot* snapshots) {
	while (snapshots) {
		struct page_snapshot* next = snapshots->next;
		slab_cache_free(snapshot_cache, snapshots);
		snapshots = next;
	}
}

void snapshot_restore_pages(struct mm* mm_struct, struct page_snapshot* snapshots) {
	if (!snapshots)
		return;

	for (struct page_snapshot* s = snapshots; s; s = s->next) {
		uintptr_t unused;

		int flags = s->vmm_flags | VMM_FIXED;
		bug(vma_map(mm_struct, s->start, s->page_size, s->mmu_flags, flags, &unused) != 0);

		unsigned long pt_flags = pagetable_mmu_to_pt(s->mmu_flags);
		if (s->page_size == HUGEPAGE_2M_SIZE)
			pt_flags |= PT_HUGEPAGE;

		if (s->physical) {
			int err = pagetable_map(mm_struct->pagetable, s->start, s->physical, pt_flags);
			if (err) {
				bug(err != -EEXIST);
				bug(pagetable_unmap(mm_struct->pagetable, s->start) != 0);
				bug(pagetable_map(mm_struct->pagetable, s->start, s->physical, pt_flags));
			}
		} else {
			pagetable_unmap(mm_struct->pagetable, s->start);
		}
	}
	snapshots_free(snapshots);
}

void snapshot_cleanup(struct page_snapshot* snapshots, bool free) {
	if (!snapshots)
		return;
	if (free) {
		for (struct page_snapshot* s = snapshots; s; s = s->next) {
			if (!(free && s->vmm_flags & VMM_ALLOC))
				continue;
			if (s->physical)
				free_pages(s->physical, get_order(s->page_size));
		}
	}
	snapshots_free(snapshots);
}
