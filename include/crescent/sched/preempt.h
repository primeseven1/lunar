#pragma once

#include <crescent/sched/kthread.h>

static inline void preempt_disable(void) {
	current_thread()->preempt_count++;
}

static inline void preempt_enable(void) {
	struct thread* current = current_thread();
	assert(current->preempt_count > 0);
	current->preempt_count--;
}
