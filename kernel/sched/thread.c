#include <crescent/common.h>
#include <crescent/sched/types.h>
#include <crescent/core/panic.h>
#include <crescent/core/limine.h>
#include <crescent/lib/string.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/vmm.h>
#include "sched.h"

static struct slab_cache* thread_cache;

thread_t* sched_thread_alloc(void) {
	thread_t* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	memset(thread, 0, sizeof(*thread));
	return thread;
}

void sched_thread_free(thread_t* thread) {
	slab_cache_free(thread_cache, thread);
}

void sched_thread_init(void) {
	thread_cache = slab_cache_create(sizeof(thread_t), _Alignof(thread_t), MM_ZONE_NORMAL, NULL, NULL);
	assert(thread_cache != NULL);
}
