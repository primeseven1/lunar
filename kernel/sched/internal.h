#pragma once

#include <lunar/sched_types.h>

#define SCHED_TICK_TIME_US 1000

void sched_policy_cpu_init(void);
void sched_thread_cache_init(void);

void topology_init(struct topology* topology, int flags);
struct cpu* topology_pick_cpu(struct topology* topology);
int topology_set_cpu(struct topology* topology, struct cpu* cpu);
