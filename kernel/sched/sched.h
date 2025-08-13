#pragma once

#include <crescent/compiler.h>
#include <crescent/sched/scheduler.h>

/* Bit 1 is reserved, and must always be set */
#define RFLAGS_DEFAULT ((1 << 1) | CPU_FLAG_INTERRUPT)

#define TIMER_TRIGGER_TIME_USEC 1000u
#define PREEMPTION_TIME_USEC 10000u
#define PREEMPT_TICKS (PREEMPTION_TIME_USEC / TIMER_TRIGGER_TIME_USEC)

void preempt_init(void);
void proc_thread_alloc_init(void);
void deferred_init(void);

struct thread* rr_pick_next(struct runqueue* rq);
void rr_enqueue_thread(struct thread* thread);
void rr_dequeue_thread(struct thread* thread);

__asmlinkage void asm_context_switch(struct context* current, struct context* next);
__asmlinkage _Noreturn void asm_kthread_start(void);

struct proc* proc_alloc(void);
void proc_free(struct proc* proc);
struct thread* thread_alloc(void);
void thread_free(struct thread* thread);

void kthread_init(struct proc* kernel_proc);
