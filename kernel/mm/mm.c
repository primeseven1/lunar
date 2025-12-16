#include <lunar/core/panic.h>
#include <lunar/core/cpu.h>
#include <lunar/asm/ctl.h>
#include <lunar/mm/mm.h>
#include <lunar/mm/slab.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include "internal.h"

static struct slab_cache* mm_cache = NULL;

void mm_cache_init(void) {
	mm_cache = slab_cache_create(sizeof(struct mm), _Alignof(struct mm), MM_ZONE_NORMAL, NULL, NULL);
	if (unlikely(!mm_cache))
		panic("mm cache init failed");
}

void __mm_init(struct mm* mm, pte_t* pagetable, void* start, void* end) {
	mm->pagetable = pagetable;
	mm->mmap_start = start;
	mm->mmap_end = end;
	list_head_init(&mm->vma_list);
	mutex_init(&mm->vma_lock);
}

struct mm* mm_create_user(void) {
	struct mm* mm = slab_cache_alloc(mm_cache);
	if (!mm)
		return NULL;
	pte_t* pagetable = hhdm_virtual(alloc_page(MM_ZONE_NORMAL));
	if (!pagetable) {
		slab_cache_free(mm_cache, mm);
		return NULL;
	}
	
	__mm_init(mm, pagetable, USER_SPACE_USABLE_START, USER_SPACE_END);
	return mm;
}

void mm_destroy(struct mm* mm) {
	free_page(hhdm_physical(mm->pagetable));
	slab_cache_free(mm_cache, mm);
}

void mm_switch(struct mm* new) {
	ctl3_write(hhdm_physical(new->pagetable));
	current_cpu()->mm_struct = new;
}
