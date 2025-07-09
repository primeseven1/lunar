#pragma once

#include <crescent/core/interrupt.h>
#include <crescent/sched/types.h>
#include <crescent/mm/vmm.h>

enum sched_flags {
	SCHED_THIS_CPU = (1 << 0), /* Only schedule the thread on the current CPU */
	SCHED_IDLE = (1 << 1), /* When set, this thread can never be killed or a panic will happen */
	SCHED_ALREADY_RUNNING = (1 << 2), /* This flag is set when setting up the scheduler, and we need to set up a thread */
	SCHED_THREAD_JOIN = (1 << 3)
};

void sched_allow_ap_cpu_scheduling(void);
void sched_init(void);
