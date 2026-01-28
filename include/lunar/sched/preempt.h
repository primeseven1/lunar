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

static inline void preempt_offset(long count) {
	barrier();
	struct thread* current = current_thread();
	bug(__builtin_add_overflow(current->preempt_count, count, &current->preempt_count));
	bug(current->preempt_count < 0);
	barrier();
}

#define HARDIRQ_SHIFT 8ul
#define SOFTIRQ_SHIFT 16ul
#define HARDIRQ_MASK (0xFFul << HARDIRQ_SHIFT)
#define SOFTIRQ_MASK (0xFFul << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET (1ul << HARDIRQ_SHIFT)
#define SOFTIRQ_OFFSET (1ul << SOFTIRQ_SHIFT)

static inline bool in_softirq(void) {
	return (current_thread()->preempt_count & SOFTIRQ_MASK) != 0;
}

static inline bool in_hardirq(void) {
	return (current_thread()->preempt_count & HARDIRQ_MASK) != 0;
}

static inline bool in_interrupt(void) {
	return in_hardirq() || in_softirq();
}

static inline bool in_atomic(void) {
	return in_interrupt() || current_thread()->preempt_count != 0 || !local_irq_enabled(local_irq_save_no_disable());
}
