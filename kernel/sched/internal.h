#pragma once

#include <crescent/compiler.h>
#include <crescent/asm/errno.h>
#include <crescent/sched/scheduler.h>

/*
 * NOTES:
 * thread_enqueue, thread_dequeue, pick_next may be called from an atomic context.
 */
struct sched_policy_ops {
	int (*init)(struct runqueue*); /* Initialize the runqueue */
	void (*thread_attach)(struct runqueue*, struct thread*, int); /* Attach a thread to a runqueue */
	void (*thread_detach)(struct runqueue*, struct thread*); /* Detach a thread from a runqueue */
	int (*enqueue)(struct runqueue*, struct thread*); /* Add a new thread to the queue */
	int (*dequeue)(struct runqueue*, struct thread*); /* Remove a thread from the queue */
	struct thread* (*pick_next)(struct runqueue*); /* Add the current thread to the queue only if running, and return the next thread */
	bool (*on_tick)(struct runqueue*, struct thread*); /* Happens on a timer interrupt, returns true if should reschedule */
	void (*on_yield)(struct runqueue*, struct thread*); /* Called when yielding (but not for sleeping/blocking) */
};

struct sched_policy {
	const char* name, *desc;
	const struct sched_policy_ops* ops;
	int prio_default, prio_min, prio_max;
	size_t thread_priv_size; /* Size of thread->policy_priv, allocated by the core */
};

#define __sched_policy __attribute__((section(".schedpolicies"), aligned(8), used))

void sched_policy_cpu_init(void);
void preempt_init(void);
void procthrd_init(void);
void ext_context_init(void);
void kthread_init(struct proc* kernel_proc);
void deferred_init_cpu(void);
void deferred_init(void);

/**
 * @brief Decide the CPU a thread should run on
 * @param flags Scheduler flags to decide the cpu
 */
struct cpu* sched_decide_cpu(int flags);

/**
 * @brief Attach a thread to the runqueue
 *
 * Not safe to call from an atomic context
 *
 * @param rq The runqueue to attach the thread to
 * @param thread The thread to attach
 * @param prio Priority of the thread
 *
 * @return -errno on failure
 */
int sched_thread_attach(struct runqueue* rq, struct thread* thread, int prio);

/**
 * @brief Detach a thread from a runqueue
 *
 * Not safe to call from an atomic context
 *
 * @param rq The runqueue to detach from
 * @param thread The thread to detach
 */
void sched_thread_detach(struct runqueue* rq, struct thread* thread);

/**
 * @brief Insert a thread into a runqueue
 *
 * Safe to call from an atomic context
 *
 * @param rq The runqueue to insert the thread into
 * @param thread The thread to insert
 *
 * @return -errno on failure
 */
int sched_enqueue(struct runqueue* rq, struct thread* thread);

/**
 * @brief Remove a thread from a runqueue
 *
 * Safe to call from an atomic context
 *
 * @param rq The runqueue to remove the thread from
 * @param thread The thread to remove
 *
 * @return -errno on failure
 */
int sched_dequeue(struct runqueue* rq, struct thread* thread);
struct thread* sched_pick_next(struct runqueue* rq);
void sched_tick(void);

struct thread* thread_create(struct proc* proc, size_t stack_size);
int thread_destroy(struct thread* thread);
int thread_set_ring(struct thread* thread, int ring);
struct proc* proc_create(void);
int proc_destroy(struct proc* proc);
void thread_add_to_proc(struct proc* proc, struct thread* thread);
static inline void thread_set_exec(struct thread* thread, void* code) {
	thread->ctx.general.rip = code;
}

void* ext_ctx_alloc(void);
void ext_ctx_free(void* ptr);

void __asmlinkage asm_context_switch(struct context* prev, struct context* next);
void context_switch(struct thread* prev, struct thread* next);

_Noreturn void __asmlinkage asm_kthread_start(void* (*func)(void*), void* arg);
