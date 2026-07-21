#include <lunar/slab.h>
#include <lunar/panic.h>
#include <lunar/sched_types.h>
#include <lunar/percpu.h>
#include <lunar/vmm.h>
#include "internal.h"

int alloc_stack(void** bottom, void** top) {
	int err = 0;

	struct page* page_array[(THREAD_STACK_SIZE >> PAGE_SHIFT) + 1];
	page_array[0] = NULL;
	for (size_t i = 1; i < ARRAY_SIZE(page_array); i++) {
		page_array[i] = page_alloc_page(MM_ZONE_NORMAL);
		if (!page_array[i]) {
			err = -ENOMEM;
			break;
		}
	}

	if (err == 0) {
		u8* mapping = vm_map(NULL, page_array, ARRAY_SIZE(page_array), PGPROT_READ | PGPROT_WRITE, VMM_STACK);
		if (!IS_PTR_ERR(mapping)) {
			*bottom = mapping;
			*top = mapping + THREAD_STACK_SIZE + PAGE_SIZE;
		} else {
			err = PTR_ERR(mapping);
		}
	}

	for (size_t i = 1; i < ARRAY_SIZE(page_array) && page_array[i]; i++)
		page_release(page_array[i]);
	return err;
}

void free_stack(void* bottom) {
	vm_unmap_force(bottom, (THREAD_STACK_SIZE + PAGE_SIZE) >> PAGE_SHIFT, 0);
}

int alloc_thread_stack(struct thread* thread, size_t off, void** bottom, void** top) {
	int err = alloc_stack(&thread->stack.kernel_stack_bottom, &thread->stack.kernel_stack_top);
	if (err)
		return err;

	if (bottom)
		*bottom = thread->stack.kernel_stack_bottom;
	if (top)
		*top = thread->stack.kernel_stack_top;
	thread->stack.kernel_ptr_off = off;

	return 0;
}

void free_thread_stack(struct thread* thread) {
	free_stack(thread->stack.kernel_stack_bottom);
}

static struct slab_cache* thread_cache = NULL;

struct thread* alloc_thread(int flags) {
	struct thread* ret = slab_cache_alloc(thread_cache);
	if (!ret)
		return NULL;

	int err = arch_context_init(&ret->context);
	if (err)
		return NULL;

	ret->stack = (struct thread_stack){
		.kernel_stack_top = NULL, .kernel_stack_bottom = NULL, .kernel_ptr_off = 0,
		.user_stack_top = NULL, .user_stack_bottom = NULL, .user_ptr_off = 0,
	};

	topology_init(&ret->topology, flags);
	struct cpu* cpu = topology_pick_cpu(&ret->topology);
	bug(cpu == NULL);
	bug(topology_set_cpu(&ret->topology, cpu) != 0);

	ret->mm_struct = NULL;
	atomic_store(&ret->proc, NULL);
	list_node_init(&ret->proc_link);
	atomic_store(&ret->prio, 0);
	ret->preempt_count = 0;

	atomic_store(&ret->state.state, THREAD_NEW);
	atomic_store(&ret->state.flags, 0);
	atomic_store(&ret->state.wakeup_errno, 0);
	atomic_store(&ret->state.sleep_gen, 0);
	list_node_init(&ret->state.block_link);

	atomic_store(&ret->refcnt, 1);
	atomic_store(&ret->policy_priv, NULL);

	return ret;
}

void free_thread(struct thread* thread) {
	bug(atomic_load(&thread->refcnt) != 0);
	arch_context_destroy(&thread->context);
	slab_cache_free(thread_cache, thread);
}

void sched_thread_cache_init(void) {
	thread_cache = slab_cache_create(sizeof(struct thread), alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	if (unlikely(!thread_cache))
		out_of_memory();
}
