#pragma once

#include <crescent/compiler.h>
#include <crescent/sched/scheduler.h>

/* LAPIC timer related things */
#define TIMER_TRIGGER_TIME_USEC 1000u
#define PREEMPTION_TIME_USEC 10000u
#define PREEMPT_TICKS (PREEMPTION_TIME_USEC / TIMER_TRIGGER_TIME_USEC)

/* Initialization functions */
void preempt_init(void);
void proc_thread_alloc_init(void);
void deferred_init(void);
void ext_context_init(void);
void kthread_init(struct proc* kernel_proc);

/* Scheduling algorithm */
struct thread* rr_pick_next(struct runqueue* rq);
void rr_enqueue_thread(struct thread* thread);
void rr_dequeue_thread(struct thread* thread);

/* Extended context (SSE, x87, etc) */
void* ext_ctx_alloc(void);
void ext_ctx_free(void* ptr);

/* Handling structures */

enum thread_rings {
	THREAD_RING_KERNEL,
	THREAD_RING_USER
};

struct thread* thread_create(struct proc* proc, size_t stack_size);
int thread_destroy(struct thread* thread);
int thread_set_ring(struct thread* thread, int ring);
static inline void thread_set_exec(struct thread* thread, void* code) {
	thread->ctx.general.rip = code;
}

struct proc* proc_create(void);
int proc_destroy(struct proc* proc);

__asmlinkage void asm_kthread_start(void* func, void* arg);
__asmlinkage void asm_context_switch(struct context* current, struct context* next);

#define RFLAGS_DEFAULT ((1 << 1) | CPU_FLAG_INTERRUPT)
