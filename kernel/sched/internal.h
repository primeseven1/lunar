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
	int (*change_prio)(struct runqueue*, struct thread*, int); /* Change the priority of a thread, returns -errno on failure */
	bool (*on_tick)(struct runqueue*, struct thread*); /* Happens on a timer interrupt, returns true if should reschedule */
	void (*on_yield)(struct runqueue*, struct thread*); /* Called when yielding (but not for sleeping/blocking) */
};

struct sched_policy {
	const char* name, *desc;
	const struct sched_policy_ops* ops;
	size_t thread_priv_size; /* Size of thread->policy_priv, allocated by the core */
};

#define __sched_policy __attribute__((section(".schedpolicies"), aligned(8), used))

void sched_policy_cpu_init(void);
void preempt_cpu_init(void);
void procthrd_init(void);
void ext_context_cpu_init(void);
void ext_context_init(void);
void kthread_init(struct proc* kernel_proc);
void workqueue_cpu_init(void);
void workqueue_init(void);
void reaper_cpu_init(void);

/**
 * @brief Decide the CPU a thread should run on
 * @param flags Scheduler flags to decide the cpu
 */
struct cpu* sched_decide_cpu(int flags);

/**
 * @brief Attach a thread to the runqueue
 *
 * Not safe to call from an atomic context.
 * Thread is attached to the process automatically.
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
 * Not safe to call from an atomic context.
 * Thread is detached from the process.
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

/**
 * @brief Pick the next thread to run
 * @param rq The runqueue to pick the thread from
 */
struct thread* sched_pick_next(struct runqueue* rq);

/**
 * @brief Called by the timer ISR
 */
void sched_tick(void);

/**
 * @brief Create a thread structure
 *
 * @param proc The process to associate with
 * @param stack_size The size of the stack for this thread
 *
 * @return A pointer to the newly created thread
 */
struct thread* thread_create(struct proc* proc, size_t stack_size);

/**
 * @brief Destroy a thread
 * @param thread The thread to destroy
 *
 * @reval 0 Success
 * @retval -EBUSY Refcount is not zero
 */
int thread_destroy(struct thread* thread);

/**
 * @brief Set a thread either in kernel mode or user mode
 *
 * @param thread The thread to set
 * @param ring THREAD_RING(USER/KERNEL)
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid ring
 */
int thread_set_ring(struct thread* thread, int ring);

/**
 * @brief Create a process struct
 * @return A pointer to the new process
 */
struct proc* proc_create(void);

/**
 * @brief Destroy a process struct
 * @param proc The process to destroy
 *
 * @retval 0 Success
 * @retval -EBUSY Process still has active threads
 */
int proc_destroy(struct proc* proc);

/**
 * @brief Set the address for the thread to start execution
 * @param thread The thread
 * @param code The pointer to where the code is located
 */
static inline void thread_set_exec(struct thread* thread, void* code) {
	thread->ctx.general.rip = code;
}

/**
 * @brief Fully attach a thread to a process
 *
 * Attaches to the process that was chosen when the thread was created.
 *
 * @param thread The thread to attach
 * @retval -EALREADY Thread already attached
 * @retval 0 Success
 */
int thread_attach_to_proc(struct thread* thread);

/**
 * @brief Fully detach a thread from a process
 *
 * @param thread The thread to detach
 * @reval -ENOENT Thread not previously attached
 * @reval 0 Success
 */
int thread_detach_from_proc(struct thread* thread);

/**
 * @brief Allocate an extended processor context
 * @return A pointer to where the context should be stored
 */
void* ext_ctx_alloc(void);

/**
 * @brief Free an extended processor context
 * @return The pointer to the context
 */
void ext_ctx_free(void* ptr);

/**
 * @brief Switch to another thread
 *
 * @param prev The current thread (usually)
 * @param next The thread to switch to
 */
void context_switch(struct thread* prev, struct thread* next);

/**
 * @brief Switch the general purpose registers and do the actual switch to the new thread
 *
 * NOTES:
 * Do not call this function. Call context_switch instead.
 *
 * @param prev The previous context
 * @param next The context to switch to
 */
void __asmlinkage asm_context_switch(struct context* prev, struct context* next);

/**
 * @brief Initial routine for kthreads
 *
 * NOTES:
 * Do not call this function ever.
 *
 * @param func The function to call after setup
 * @param arg The argument to pass to the function
 */
_Noreturn void __asmlinkage asm_kthread_start(void* (*func)(void*), void* arg);
