#include <lunar/slab.h>
#include <lunar/panic.h>
#include <lunar/sched_types.h>
#include <lunar/percpu.h>
#include "internal.h"

static struct slab_cache* thread_cache = NULL;

struct thread* sched_thread_alloc(int flags) {
	struct thread* ret = slab_cache_alloc(thread_cache);
	if (!ret)
		return NULL;

	int err = arch_context_init(&ret->context);
	if (err)
		return NULL;

	ret->stack = (struct thread_stack){
		.kernel_stack_top = NULL,
		.kernel_ptr_off = 0,
		.kernel_size = 0, .kernel_guard_size = 0,
		.user_stack_top = NULL,
		.user_ptr_off = 0,
		.user_size = 0, .user_guard_size = 0
	};

	topology_init(&ret->topology, flags);
	struct cpu* cpu = topology_pick_cpu(&ret->topology);
	bug(cpu == NULL);
	bug(topology_set_cpu(&ret->topology, cpu) != 0);

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

void sched_thread_destroy(struct thread* thread) {
	bug(atomic_load(&thread->refcnt) != 0);
	arch_context_destroy(&thread->context);
	slab_cache_free(thread_cache, thread);
}

void sched_thread_cache_init(void) {
	thread_cache = slab_cache_create(sizeof(struct thread), alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	if (unlikely(!thread_cache))
		out_of_memory();
}
