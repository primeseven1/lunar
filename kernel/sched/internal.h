#pragma once

#include <lunar/sched_types.h>

#define SCHED_TICK_TIME_US 1000
#define SCHED_THREAD_ATTACHED_REFCNT 2

void sched_policy_cpu_init(void);
void sched_proctbl_init(void);
void sched_thread_cache_init(void);
void sched_proc_cache_init(void);

void sched_thread_attach_to_proc(struct proc* proc, struct thread* thread);
void sched_thread_detach_from_proc(struct thread* thread);

pid_t sched_alloc_pid(void);
void sched_free_pid(pid_t pid);

void sched_thread_topology_init(struct thread* thread, int flags);
struct cpu* sched_topology_pick_cpu(struct thread* thread);
