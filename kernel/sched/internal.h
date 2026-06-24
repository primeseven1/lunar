#pragma once

#include <lunar/sched_types.h>

#define SCHED_TICK_TIME_US 1000

void sched_policy_cpu_init(void);
void sched_thread_cache_init(void);
void sched_thread_topology_init(struct thread* thread, int flags);
struct cpu* sched_topology_pick_cpu(struct thread* thread);
