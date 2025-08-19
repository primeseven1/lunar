#pragma once

#include <crescent/core/spinlock.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/timekeeper.h>
#include <crescent/lib/list.h>

struct cpu;
typedef int pid_t;
typedef int tid_t;

enum thread_states {
	THREAD_NEW,
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_BLOCKED,
	THREAD_SLEEPING,
	THREAD_ZOMBIE
};

enum sched_flags {
	SCHED_THIS_CPU = (1 << 0),
	SCHED_CPU0 = (1 << 1),
};

struct work {
	void (*fn)(void*);
	void* arg;
};

struct proc {
	pid_t pid; /* Process ID */
	struct mm* mm_struct; /* Memory manager context */
	u8* tid_map; /* Thread ID bitmap */
	struct list_head threads; /* The list of threads for this process, linked with proc_link */
	atomic(unsigned long) thread_count; /* The number of threads for this process */
	spinlock_t thread_lock; /* For the thread linked list */
};

struct thread {
	tid_t id; /* Thread ID */
	struct cpu* target_cpu; /* What queue this thread is in */
	struct proc* proc; /* The process associated with this thread */
	unsigned long cpu_mask; /* What CPU's this thread can run on */
	time_t time_slice, wakeup_time; /* Number of ticks the process has, wakeup time is for sleeping */
	atomic(int) state, wakeup_err; /* State and the wakeup error code */
	atomic(bool) interruptable; /* Can be interrupted by signals */
	void* stack; /* Base address of the stack, not the top */
	size_t stack_size; /* Size of the stack including the gaurd page(s) */
	struct {
		struct context general; /* General purpose registers with interrupt context */
		void* extended; /* SSE/AVX/etc */
	} ctx;
	long preempt_count; /* Task cannot be preempted if not zero */
	struct list_node queue_link; /* next/prev in the CPU queue */
	struct list_node proc_link; /* next/prev in the process struct */
	struct list_node sleep_link; /* delibrate sleeping via schedule_sleep() */
	struct list_node zombie_link; /* waiting to be cleaned up */
	struct list_node blocked_link; /* scheduler info on blocked threads */
	struct list_node external_blocked_link; /* used by things like mutexes/semaphores */
	atomic(unsigned long) refcount; /* can't be cleaned up until zero */
};

struct runqueue {
	struct thread* current, *idle;
	atomic(unsigned long) thread_count;
	struct list_head queue;
	struct list_head sleeping;
	struct list_head zombie;
	struct list_head blocked;
	spinlock_t lock;
};

/**
 * @brief Switch to another thread
 * @param thread The thread to switch to
 */
void context_switch(struct thread* thread);

/**
 * @brief Stop the current thread
 */
_Noreturn void thread_exit(void);

/**
 * @brief Block the current thread without yielding
 *
 * This function allows you to do things like release a lock before calling schedule().
 * Interrupts should be disabled before calling this function, so that way the thread isn't
 * preempted before finishing cleanup.
 *
 * @param interruptable Determines if the thread can be interrupted by signals
 */
void thread_block_noyield(bool interruptable);

/**
 * @brief Block the current thread
 *
 * @retval -EINTR Interrupted by a signal
 * @retval -ETIMEDOUT The time limit was hit (like from a mutex or semaphore)
 * @retval 0 Unblocked normally
 */
int thread_block(bool interruptable);

/**
 * @brief Sleep the current thread for a certian amount of time without yielding
 *
 * This allows for cleaning up resources before calling schedule().
 * Interrupts should be disabled before calling this function, so that way the thread isn't
 * preempted before finishing cleanup.
 *
 * @param ms The number of milliseconds to sleep for
 * @param interruptable Determines if this sleep can be interrupted by signals
 */
void thread_sleep_noyield(time_t ms, bool interruptable);

/**
 * @brief Sleep the current thread for a certian amount of time
 *
 * @param ms The number of milliseconds to sleep for
 * @param interruptable Determines if the thread can be interrupted by signals
 *
 * @retval -EINTR Sleep was interrupted by a signal
 * @retval -ETIMEDOUT Shouldn't really happen
 * @retval 0 Thread woke up normally
 */
int thread_sleep(time_t ms, bool interruptable);

/**
 * @brief Wake up a thread
 *
 * @param thread The thread to wake up
 */
int thread_wakeup(struct thread* thread, int errno);

struct thread* atomic_schedule(void);
int schedule(void);
void schedule_thread(struct thread* thread, int flags);
int schedule_work(void (*fn)(void*), void* arg, int flags);

void deferred_init_cpu(void);
void scheduler_init_cpu(void);
void scheduler_init(void);
