#include <crescent/mm/buddy.h>
#include <crescent/mm/hhdm.h>
#include <crescent/core/panic.h>
#include "internal.h"

static void prevpage_create(struct prevpage** head, void* virtual,
		physaddr_t physical, size_t page_size, mmuflags_t mmu_flags, int vmm_flags) {
	physaddr_t _page = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(**head)));
	struct prevpage* page = hhdm_virtual(_page);

	page->start = virtual;
	page->physical = physical;
	page->page_size = page_size;
	page->mmu_flags = mmu_flags;
	page->vmm_flags = vmm_flags;

	page->next = *head;
	*head = page;
}

static void __prevpage_save(struct mm* mm_struct, struct prevpage** head, void* virtual) {
	struct vma* vma = vma_find(mm_struct, virtual);
	if (!vma) /* fine */
		return;

	size_t page_size = vma->flags & VMM_HUGEPAGE_2M ? HUGEPAGE_2M_SIZE : PAGE_SIZE;
	physaddr_t physical = pagetable_get_physical(mm_struct->pagetable, virtual);

	/* Make sure the page isn't already in the list */
	uintptr_t virtual_start = (uintptr_t)virtual;
	uintptr_t virtual_end = virtual_start + page_size;
	for (struct prevpage* current = *head; current; current = current->next) {
		uintptr_t current_start = (uintptr_t)current->start;
		uintptr_t current_end = current_start + current->page_size;

		/* also also fine */
		if (!(virtual_end <= current_start || virtual_start >= current_end))
			return;
	}

	prevpage_create(head, virtual, physical, page_size, vma->prot, vma->flags);
}

static void prevpage_free_all(struct prevpage* head) {
	while (head) {
		struct prevpage* next = head->next;
		free_pages(hhdm_physical(head), get_order(sizeof(*head)));
		head = next;
	}
}

struct prevpage* prevpage_save(struct mm* mm_struct, u8* virtual, size_t size) {
	struct prevpage* prev_pages = NULL;
	u8* end = virtual + size;
	while (virtual < end) {
		__prevpage_save(mm_struct, &prev_pages, virtual);
		virtual += PAGE_SIZE;
	}
	return prev_pages;
}

void prevpage_fail(struct mm* mm_struct, struct prevpage* head) {
	struct prevpage* tmp = head;

	pte_t* pagetable = mm_struct->pagetable;
	while (head) {
		void* _unused;

		/*
		 * Use the noreplace flag here, since we already know there is no VMA there.
		 * Using noreplace can be used for a bug check.
		 */
		assert(vma_map(mm_struct, head->start, head->page_size, 
				head->mmu_flags, head->vmm_flags | VMM_FIXED | VMM_NOREPLACE, 
				&_unused) == 0);

		/* Now just remap the page */
		unsigned long pt_flags = pagetable_mmu_to_pt(head->mmu_flags);
		if (head->vmm_flags & VMM_HUGEPAGE_2M)
			pt_flags |= PT_HUGEPAGE;
		assert(pagetable_map(pagetable, head->start, head->physical, pt_flags) == 0);
		tlb_flush_range(head->start, head->page_size);

		head = head->next;
	}

	prevpage_free_all(tmp);
}

void prevpage_success(struct prevpage* head, int flags) {
	struct prevpage* tmp = head;
	while (head) {
		if (flags & PREVPAGE_FREE_PREVIOUS && head->vmm_flags & VMM_ALLOC) {
			unsigned int order = get_order(head->page_size);
			free_pages(head->physical, order);
		}

		head = head->next;
	}

	prevpage_free_all(tmp);
}
