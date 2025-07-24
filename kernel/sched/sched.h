#pragma once

#include <crescent/compiler.h>

void sched_preempt_init(void);
void sched_create_init(void);

/**
 * @brief Allocate a process struct
 *
 * Automatically assigns a PID to the new process
 *
 * @return A new process struct
 */
struct proc* sched_proc_alloc(void);

/**
 * @brief Free a process struct
 * @param proc The process struct to free
 */
void sched_proc_free(struct proc* proc);

/**
 * @brief Allocate a thread struct
 * @return The new thread struct
 */
struct thread* sched_thread_alloc(void);

/**
 * @brief Free a thread struct
 * @param thread The thread to free
 */
void sched_thread_free(struct thread* thread);

/**
 * @brief Select a new thread to be scheduled
 * @param start The thread to start searching from
 * @return The new thread selected
 */
struct thread* select_new_thread(struct thread* start);

/**
 * @brief Schedule a new thread
 *
 * The refcount is not modified by this function.
 *
 * @param thread The thread to schedule
 * @param proc The process to associate this thread with. NULL means the kernel process
 * @param flags Scheduler flags
 *
 * @return -errno on failure, 0 on success
 */
int schedule_thread(struct thread* thread, struct proc* proc, int flags);

__asmlinkage _Noreturn void asm_kthread_start(void);
