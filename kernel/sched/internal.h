#pragma once

#include <lunar/compiler.h>
#include <lunar/asm/errno.h>
#include <lunar/core/cpu.h>
#include <lunar/sched/policy.h>

#define KERNEL_PID 0
#define THREAD_RFLAGS_DEFAULT 0x202

void sched_policy_cpu_init(void);
void preempt_cpu_init(void);
void procthrd_init(void);
void ext_context_cpu_init(void);
void ext_context_init(void);
void sched_proctbl_init(void);
void kthread_init(void);
void workqueue_cpu_init(void);
void workqueue_init(void);
void reaper_cpu_init(void);

/**
 * @brief Send a reschedule IPI to a CPU
 * @param cpu The CPU to send the IPI to
 */
void sched_send_resched(struct cpu* target);

/**
 * @brief Attach a thread to the runqueue
 *
 * Not safe to call from an atomic context. This function also adds the thread to the associated process.
 * Adds two refs (from attaching to the runqueue, and to the proc).
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
 * Not safe to call from an atomic context. Also removes the thread from the process struct.
 * Removes two refs (runqueue and proc).
 *
 * @param rq The runqueue to detach from
 * @param thread The thread to detach
 */
void sched_thread_detach(struct runqueue* rq, struct thread* thread);

/**
 * @brief Insert a thread into a runqueue
 *
 * Safe to call from an atomic context.
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
 * Safe to call from an atomic context.
 *
 * @param rq The runqueue to remove the thread from
 * @param thread The thread to remove
 *
 * @return -errno on failure
 */
int sched_dequeue(struct runqueue* rq, struct thread* thread);

/**
 * @brief Pick the next thread to run
 *
 * Adds the current thread into the runqueue (unless the thread is no longer runnable).
 * Not safe to call from a normal context.
 *
 * @param rq The runqueue to pick the thread from
 * @return The next thread that should be ran.
 */
struct thread* sched_pick_next(struct runqueue* rq);

/**
 * @brief Called by the timer ISR
 */
void sched_tick(void);

/**
 * @brief Create a thread structure
 *
 * The thread is returned with a ref. Once the thread is actually scheduled,
 * the reaper thread calls thread_destroy for you. However, you still need to
 * remove the ref to the thread.
 *
 * @param proc The process to associate with
 * @param stack_size The size of the stack for this thread
 * @param topology_flags The flags for how the CPU should run
 *
 * @return A pointer to the newly created thread
 */
struct thread* thread_create(struct proc* proc, size_t stack_size, int topology_flags);

/**
 * @brief Prepare a kernel thread for execution
 *
 * @param thread The thread to prepare
 * @param exec Where to start execution from
 */
void thread_prep_exec_kernel(struct thread* thread, void* exec);

/**
 * @brief Prepare a user thread for execution
 *
 * @param thread The thread to prepare
 * @param exec Where to start execution from, must be a user pointer
 */
void thread_prep_exec_user(struct thread* thread, void __user* exec);

/**
 * @brief Destroy a thread
 * @param thread The thread to destroy
 *
 * @reval 0 Success
 * @retval -EBUSY Refcount is not zero
 */
int thread_destroy(struct thread* thread);

/**
 * @brief Adds a thread to the process's list
 *
 * Process is taken from thread->proc
 *
 * @param thread The thread to attach
 *
 * @retval -EALREADY Thread already attached
 * @retval 0 Success
 */
int thread_add_to_proc(struct thread* thread);

/**
 * @brief Remove a thread from a process list
 *
 * Process is taken from thread->proc
 *
 * @param thread The thread to remove
 *
 * @reval -ENOENT Thread not previously added
 * @reval 0 Success
 */
int thread_remove_from_proc(struct thread* thread);

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

int tid_create_bitmap(struct proc* proc);
void tid_free_bitmap(struct proc* proc);
pid_t pid_alloc(void);
int pid_free(pid_t id);
tid_t tid_alloc(struct proc* proc);
int tid_free(struct proc* proc, tid_t id);
