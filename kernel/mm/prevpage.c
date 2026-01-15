#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/core/panic.h>
#include "internal.h"

static void prevpage_free_all(struct prevpage* head) {
	while (head) {
		struct prevpage* next = head->next;
		free_pages(hhdm_physical(head), get_order(sizeof(*head)));
		head = next;
	}
}

struct prevpage* prevpage_save(struct mm* mm_struct, uintptr_t virtual, size_t size) {
	struct prevpage* prev_pages = NULL;
	size_t max_add = (size_t)(UINTPTR_MAX - (uintptr_t)virtual);
	if (size > max_add)
		size = max_add;

	uintptr_t end = virtual + size;
	while (virtual < end) {
		struct vma* vma = vma_find(mm_struct, virtual);
		if (!vma) {
			virtual += PAGE_SIZE;
			continue;
		}

		uintptr_t vma_end = vma->top;
		uintptr_t range_end = vma_end < end ? vma_end : end;

		while (virtual < range_end) {
			bool vma_huge = !!(vma->flags & VMM_HUGEPAGE_2M);
			bool aligned_2m = ((uintptr_t)virtual & (HUGEPAGE_2M_SIZE - 1)) == 0;
			size_t node_ps = (vma_huge && aligned_2m && (size_t)(range_end - virtual) >= HUGEPAGE_2M_SIZE) ? HUGEPAGE_2M_SIZE : PAGE_SIZE;

			/* Even if phys is NULL, we still need to save, since we need the VMA */
			physaddr_t phys = pagetable_get_physical(mm_struct->pagetable, virtual);
			struct prevpage* p = hhdm_virtual(alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(*p))));
			p->start = virtual;
			p->physical = phys;
			p->page_size = node_ps;
			p->mmu_flags = vma->prot;
			p->vmm_flags = vma->flags;
			p->len = node_ps;

			/* Extend while contiguous if present */
			bool base_present = phys != 0;
			for (uintptr_t current = virtual + node_ps; current + node_ps <= range_end; current += node_ps) {
				physaddr_t nphys = pagetable_get_physical(mm_struct->pagetable, current);
				bool current_present = (nphys != 0);
				if (current_present != base_present)
					break;

				/* Require physical contiguity */
				if (base_present) {
					physaddr_t expect = p->physical + (current - p->start);
					if (nphys != expect)
						break;
				}

				p->len += node_ps;
			}

			p->next = prev_pages;
			prev_pages = p;

			virtual += p->len;
		}
	}

	return prev_pages;
}

void prevpage_fail(struct mm* mm_struct, struct prevpage* head) {
	for (struct prevpage* p = head; p; p = p->next) {
		uintptr_t unused;
		int flags = p->vmm_flags | VMM_FIXED | VMM_NOREPLACE;
		bug(vma_map(mm_struct, p->start, p->len, p->mmu_flags, flags, &unused) != 0);

		unsigned long pt_flags = pagetable_mmu_to_pt(p->mmu_flags);
		if (p->page_size == HUGEPAGE_2M_SIZE)
			pt_flags |= PT_HUGEPAGE;

		size_t count = p->len / p->page_size;
		while (count--) {
			if (p->physical) {
				bug(pagetable_map(mm_struct->pagetable, p->start, p->physical, pt_flags) != 0);
				p->physical += p->page_size;
			} else {
				pagetable_unmap(mm_struct->pagetable, p->start);
			}
			p->start = p->start + p->page_size;
		}
	}

	prevpage_free_all(head);
}

void prevpage_success(struct prevpage* head, int flags) {
	struct prevpage* tmp = head;
	while (head) {
		if (flags & PREVPAGE_FREE_PREVIOUS && head->vmm_flags & VMM_ALLOC) {
			unsigned int order = get_order(head->page_size);
			physaddr_t base = head->physical;
			if (base) {
				for (size_t off = 0; off < head->len; off += head->page_size)
					free_pages(base + off, order);
			}
		}

		head = head->next;
	}

	prevpage_free_all(tmp);
}
