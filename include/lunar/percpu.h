#pragma once

#include <arch/percpu.h>
#include <arch/irq_flags.h>
#include <lunar/mm.h>
#include <lunar/time.h>
#include <lunar/sched_policy.h>
#include <lunar/interrupt.h>

struct cpu {
	struct mm* mm_struct;
	struct timekeeper_source* timekeeper;
	struct runqueue runqueue;
	struct list_head timer_event_list, softirq_timer_cb_list;
	u16 softirq_mask;
	struct semaphore softirqd_sem;
	long softirq_count;
	bool need_resched;
	struct arch_cpu arch_specific;
};
static_assert(sizeof(((struct cpu*)0)->softirq_mask) >= SOFTIRQ_COUNT, "sizeof(((struct cpu*)0)->softirq_mask) >= SOFTIRQ_COUNT");

/**
 * @brief Get the current per-cpu structure
 *
 * This function is not safe to call with preempt enabled.
 * Either disable IRQ's or disable preempt, depending on the context.
 */
static inline struct cpu* current_cpu(void) {
	return arch_current_cpu();
}
