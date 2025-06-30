#include <crescent/common.h>
#include <crescent/sched/types.h>
#include <crescent/core/panic.h>
#include <crescent/core/limine.h>
#include <crescent/lib/string.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/vmm.h>
#include "sched.h"

static struct slab_cache* thread_cache;

struct thread* sched_thread_create(void) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	memset(thread, 0, sizeof(*thread));
	return thread;
}

void sched_thread_destroy(struct thread* thread) {
	slab_cache_free(thread_cache, thread);
}

void sched_thread_init(void) {
	thread_cache = slab_cache_create(sizeof(struct thread), _Alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	assert(thread_cache != NULL);
}
