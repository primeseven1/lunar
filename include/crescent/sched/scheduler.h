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
	SCHED_SLEEP_INTERRUPTIBLE = (1 << 2),
	SCHED_SLEEP_BLOCK = (1 << 3)
};

enum thread_rings {
	THREAD_RING_KERNEL,
	THREAD_RING_USER
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
	unsigned long cpu_mask;
	bool attached; /* Attached to the policy? */
	struct proc* proc; /* The process struct this thread is linked to */
	int ring; /* Kernel mode or user mode thread */
	atomic(int) state; /* ready, blocked, running, etc.. */
	time_t wakeup_time; /* When the thread should wake up in nanoseconds */
	atomic(int) wakeup_err; /* Wakeup error code (eg. -ETIMEDOUT, -EINTR)*/
	atomic(bool) sleep_interruptable; /* Can be interrupted by signals */
	long preempt_count; /* Task can be preempted when zero */
	void* stack; /* Base address of the stack */
	size_t stack_size;
	struct {
		struct context general; /* General purpose registers */
		void* extended; /* SSE, AVX, etc.. */
	} ctx; /* For the task switcher, obviously */
	struct list_node proc_link; /* Link for proc->threads */
	struct list_node sleep_link; /* Link so the scheduler can wake up threads */
	struct list_node block_link; /* Link for things like mutexes/semaphores */
	struct list_node zombie_link; /* For reaper thread */
	void* policy_priv; /* For the scheduling algorithm */
	atomic(unsigned long) refcount;
};

struct sched_policy;

struct runqueue {
	const struct sched_policy* policy;
	struct thread* current, *idle;
	struct list_head sleepers; /* Sleeping threads, may also contain blocked threads for timeouts */
	struct list_head zombies; /* For reaper thread */
	void* policy_priv; /* For scheduling algorithm */
	spinlock_t lock;
};

void sched_init(void);

/**
 * @brief Switch to another thread.
 *
 * @return Why the thread was woken up. -EAGAIN means preemptions were disabled when this function was called.
 */
int schedule(void);

/**
 * @brief Schedule work in a deferred workqueue
 *
 * @param fn The function to execute
 * @param arg The argument to pass to the function
 * @param flags Scheduler flags
 *
 * @return -errno on failure
 */
int schedule_work(void (*fn)(void*), void* arg, int flags);

/**
 * @brief Relinquish the CPU
 *
 * Notes:
 * Do not use this function for sleeping/blocking.
 *
 * @return Always zero.
 */
int sched_yield(void);

/**
 * @brief Prepare for a sleep
 *
 * Notes:
 * Do NOT use sched_yield() after preparing for sleep. Use schedule() instead.
 *
 * @param ms The number of milliseconds to sleep for (or a timeout if flags & SCHED_SLEEP_BLOCK is set and ms is not zero).
 * @param flags Flags for the sleep (SCHED_SLEEP_BLOCK, SCHED_SLEEP_INTERRUPTABLE)
 */
void sched_prepare_sleep(time_t ms, int flags);

int sched_wakeup(struct thread* thread, int wakeup_err);

/**
 * @breif Stop the current thread and make it a zombie.
 */
_Noreturn void sched_thread_exit(void);
