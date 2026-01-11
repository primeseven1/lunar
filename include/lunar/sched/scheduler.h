#pragma once

#include <lunar/common.h>
#include <lunar/core/abi.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/panic.h>
#include <lunar/core/cred.h>
#include <lunar/sched/procthrd.h>
#include <lunar/lib/list.h>

struct cpu;

#define SCHED_PRIO_MIN 1
#define SCHED_PRIO_MAX 99
#define SCHED_PRIO_DEFAULT 45

enum sched_sleep_flags {
	SCHED_SLEEP_INTERRUPTIBLE = (1 << 0),
	SCHED_SLEEP_BLOCK = (1 << 1)
};

struct work {
	void (*fn)(void*);
	void* arg;
	struct list_node link;
};

void sched_cpu_init(void);
void sched_init(void);

void atomic_context_switch(struct thread* prev, struct thread* next, struct context* ctx);

struct thread* atomic_schedule(void);

/**
 * @brief Switch to another runnable thread.
 *
 * If the thread was not prepared for sleep (via sched_prepare_for_sleep()), then the return value of this
 * function is undefined and should be ignored in this case. A bug is triggered if in_interrupt() returns true.
 *
 * @retval 0 Thread woke up normally
 * @retval -EAGAIN Preemptions are disabled, a bug is triggered if the current task isn't runnable.
 * @retval -EINTR Sleep was interrupted by a signal
 * @retval -ETIMEDOUT A timeout was hit
 */
int schedule(void);

/**
 * @brief Schedule deferred work in a global workqueue
 *
 * @param fn The function to execute
 * @param arg The argument to pass to the function
 *
 * @retval 0 Successful
 * @retval -EAGAIN Workqueue was full
 */
int sched_workqueue_add(void (*fn)(void*), void* arg);

/**
 * @brief Schedule deferred work on a specific CPU
 *
 * @param cpu The CPU to run on
 * @param fn The function to execute
 * @param arg The argument to pass to the function
 *
 * @retval 0 Successful
 * @retval -EAGAIN Workqueue was full
 */
int sched_workqueue_add_on(struct cpu* cpu, void(*fn)(void*), void* arg);

/**
 * @brief Relinquish the CPU
 *
 * Do not use this function for sleeping/blocking.
 *
 * @return Always zero
 */
int sched_yield(void);

/**
 * @brief Prepare for a sleep
 *
 * Do NOT use sched_yield() after preparing for sleep. Use schedule() instead.
 *
 * @param ms The number of milliseconds to sleep for (or a timeout if flags & SCHED_SLEEP_BLOCK is set and ms is not zero).
 * @param flags Flags for the sleep (SCHED_SLEEP_*)
 *
 * @retval -EINVAL ms is zero and SCHED_SLEEP_BLOCK isn't set
 * @retval 0 Successful
 */
int sched_prepare_sleep(time_t ms, int flags);

/**
 * @brief Wake up a thread
 *
 * If the thread is already woken up, this is a no-op.
 *
 * @param thread The thread to wake up
 * @param wakeup_err The reason the thread is waking up
 *
 * @retval -EINVAL wakeup_err is not 0, -EINTR, or -ETIMEDOUT
 * @retval 0 Successful
 */
int sched_wakeup(struct thread* thread, int wakeup_err);

/**
 * @brief Change the priority of a thread
 *
 * @param thread The thread to change the priority of
 * @param The priority
 *
 * @return -errno on failure
 */
int sched_change_prio(struct thread* thread, int prio);

/**
 * @breif Stop the current thread and make it a zombie.
 */
_Noreturn void sched_thread_exit(void);

/**
 * @brief Create a process struct
 *
 * @param cred The cred struct for the user
 * @param mm_struct The memory management structure pointer
 * @param out Where the pointer to the allocated process will be stored
 *
 * @retval -EAGAIN Cannot allocate a PID
 * @retval -ENOMEM Out of memory
 * @retval 0 Successful
 */
int sched_proc_create(const struct cred* cred, struct mm* mm_struct, struct proc** out);

/**
 * @brief Destroy a process struct
 * @param proc The process to destroy
 *
 * @retval 0 Success
 * @retval -EBUSY Process still has active threads
 */
int sched_proc_destroy(struct proc* proc);

/**
 * @brief Add a process to the global table
 * @param proc The process to add
 * @return -errno on failure
 */
int sched_add_to_proctbl(struct proc* proc);

/**
 * @brief Get a process from a table
 *
 * @param pid The PID
 *
 * @retval -ESRCH No such process
 * @retval 0 Successful
 */
struct proc* sched_get_from_proctbl(pid_t pid);

/**
 *
 */
int sched_remove_proctbl(pid_t pid);
