#pragma once

#include <crescent/sched/scheduler.h>

/**
 * @brief Create a kernel thread
 *
 * @param sched_flags Scheduler flags
 * @param func The start routine
 * @param arg The argument to pass to the function
 *
 * @return A pointer to the new thread
 */
struct thread* kthread_create(int sched_flags, void* (*func)(void*), void* arg);

/**
 * @brief Detach a kernel thread
 *
 * After a kthread is detached, it is not safe to do anything with the thread structure.
 *
 * @param thread The thread to detach
 */
static inline void kthread_detach(struct thread* thread) {
	atomic_sub_fetch(&thread->refcount, 1, ATOMIC_RELEASE);
}

/**
 * @brief Exit a kernel thread
 * @param ret The return value for the creator of the thread
 */
_Noreturn void kthread_exit(void* ret);

/**
 * @brief Get the return value from a thread
 *
 * If the thread has not finished executing, the current thread yields until
 * the thread exits.
 *
 * @param thread The thread to get the return value from
 * @param thread The thread
 */
void* kthread_join(struct thread* thread);
