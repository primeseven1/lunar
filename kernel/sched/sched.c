#include <crescent/common.h>
#include <crescent/mm/slab.h>
#include <crescent/core/panic.h>
#include <crescent/sched/sched.h>
#include <crescent/core/printk.h>
#include <crescent/lib/string.h>
#include <crescent/asm/segment.h>
#include "timer.h"

static struct slab_cache* task_cache = NULL;

void sched_init(void) {
	task_cache = slab_cache_create(sizeof(struct task), _Alignof(struct task), MM_ZONE_NORMAL, NULL, NULL);
	if (!task_cache)
		panic("Failed to create task cache");

	timer_init();
}
