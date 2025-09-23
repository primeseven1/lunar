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

#define HARDIRQ_SHIFT 8ul
#define SOFTIRQ_SHIFT 16ul
#define HARDIRQ_MASK (0xFFul << HARDIRQ_SHIFT)
#define SOFTIRQ_MASK (0xFFul << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET (1ul << HARDIRQ_SHIFT)
#define SOFTIRQ_OFFSET (1ul << SOFTIRQ_SHIFT)

static inline bool in_interrupt(void) {
	return (current_thread()->preempt_count & (HARDIRQ_MASK | SOFTIRQ_MASK)) != 0;
}
