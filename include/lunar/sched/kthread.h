#pragma once

#include <lunar/sched/scheduler.h>
#include <lunar/core/cpu.h>

static inline struct thread* current_thread(void) {
	irqflags_t irq = local_irq_save();
	struct thread* current = current_cpu()->runqueue.current;
	local_irq_restore(irq);
	return current;
}

/**
 * @brief Create a kernel thread
 *
 * @param sched_flags Scheduler flags
 * @param func The start routine
 * @param arg The argument to pass to the function
 * @param fmt Format string for name
 * @param ... Variable arguments for name
 *
 * @return -errno on failure, otherwise the thread ID is returned
 * @retval -ENOMEM No memory
 */
__attribute__((format(printf, 4, 5)))
tid_t kthread_create(int sched_flags, int (*func)(void*), void* arg, const char* fmt, ...);

/**
 * @brief Detach a kernel thread
 *
 * Allows a thread to have its resources cleaned up after execution.
 *
 * @param thread The thread to detach
 *
 * @reval -ESRCH Thread not found
 * @retval 0 Successful
 */
int kthread_detach(tid_t id);

/**
 * @brief Exit a kernel thread
 * @param code The exit code, here for convention only. Discarded.
 */
_Noreturn void kthread_exit(int code);

/**
 * @brief Wait for a thread to finish execution
 *
 * If the thread has not finished executing, the current thread yields until
 * the thread exits. Doesn't decrease the refcount.
 *
 * @param id The thread ID
 *
 * @retval -ESRCH Thread not found
 * @retval 0 Successful
 */
int kthread_wait_for_completion(tid_t id);
