#pragma once

#include <crescent/sched/sched.h>

void sched_proc_init(void);
void sched_thread_init(void);
void sched_timer_init(void);

struct proc* sched_proc_create(void);
void sched_proc_destroy(struct proc* proc);
struct thread* sched_thread_create(void);
void sched_thread_destroy(struct thread* thread);

void sched_schedule(struct thread* thread, struct proc* proc);
void sched_switch(struct context* ctx);
