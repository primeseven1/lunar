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
 * The kthread will have one ref for its internal reference. This ref can be dropped by calling
 * kthread_detach(). However, after this is done, nothing should be done with the thread.
 *
 * @param topology_flags How the topology is decided
 * @param func The start routine
 * @param arg The argument to pass to the function
 * @param fmt Format string for name
 * @param ... Variable arguments for name
 *
 * @return A pointer to the thread. The thread has 2 refs
 */
__attribute__((format(printf, 4, 5)))
struct thread* kthread_create(int topology_flags, int (*func)(void*), void* arg, const char* fmt, ...);

/**
 * @brief Run a kernel thread
 *
 * @param thread The thread to run
 * @param prio The priority to run the thread at
 *
 * @retval -EINVAL Not a kthread
 * @retval 0 Successful
 */
int kthread_run(struct thread* thread, int prio);

/**
 * @brief Destroy a kernel thread
 *
 * Do NOT call this function if the thread is scheduled.
 *
 * @param thread The thread to destroy
 */
void kthread_destroy(struct thread* thread);

/**
 * @brief Detach a kernel thread
 *
 * Drops the internal kthread reference to thread. This function
 * should only be called if the thread is scheduled (with kthread_run())
 *
 * @param thread The thread to detach
 */
void kthread_detach(struct thread* thread);

/**
 * @brief Exit a kernel thread
 * @param code The exit code, here for convention only. Discarded.
 */
_Noreturn void kthread_exit(int code);
