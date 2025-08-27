#pragma once

#include <crescent/core/cpu.h>

static inline void preempt_disable(void) {
	current_cpu()->runqueue.current->preempt_count++;
}

static inline void preempt_enable(void) {
	struct thread* current = current_cpu()->runqueue.current;
	assert(current->preempt_count > 0);
	current->preempt_count--;
}
