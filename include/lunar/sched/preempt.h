#pragma once

#include <lunar/sched/kthread.h>

static inline void preempt_disable(void) {
	current_thread()->preempt_count++;
	barrier();
}

static inline void preempt_enable(void) {
	barrier();
	struct thread* current = current_thread();
	bug(current->preempt_count == 0);
	current->preempt_count--;
}
