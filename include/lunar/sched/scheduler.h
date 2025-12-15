#pragma once

#include <lunar/common.h>
#include <lunar/core/abi.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/panic.h>
#include <lunar/core/cred.h>
#include <lunar/lib/list.h>

struct cpu;

#define SCHED_PRIO_MIN 1
#define SCHED_PRIO_MAX 99
#define SCHED_PRIO_DEFAULT 45

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
	SCHED_CPU0 = (1 << 1)
};

enum sched_sleep_flags {
	SCHED_SLEEP_INTERRUPTIBLE = (1 << 1),
	SCHED_SLEEP_BLOCK = (1 << 2)
};

enum thread_rings {
	THREAD_RING_KERNEL,
	THREAD_RING_USER
};

struct work {
	void (*fn)(void*);
	void* arg;
	struct list_node link;
};

struct proc {
	pid_t pid; /* Process ID */
	struct mm* mm_struct; /* Memory manager context */
	struct cred cred;
	u8* tid_map; /* Thread ID bitmap */
	spinlock_t tid_lock;
	struct list_head threads; /* The list of threads for this process, linked with proc_link */
	atomic(unsigned long) thread_count; /* The number of threads for this process, don't write without locking first */
	spinlock_t thread_lock; /* For the thread linked list */
};

struct thread {
	void* utk_stack_top; /* User to kernel stack */
	tid_t id; /* Thread ID */
	struct cpu* target_cpu; /* What queue this thread is in */
	unsigned long cpu_mask;
	bool attached; /* Attached to the policy? */
	struct proc* proc; /* The process struct this thread is linked to */
	bool in_usercopy; /* For the page fault handler */
	int ring; /* Kernel mode or user mode thread */
	int prio; /* Priority of the current thread */
	atomic(int) state; /* ready, blocked, running, etc.. */
	time_t wakeup_time; /* When the thread should wake up in nanoseconds */
	atomic(int) wakeup_err; /* Wakeup error code (eg. -ETIMEDOUT, -EINTR)*/
	atomic(bool) sleep_interruptable; /* Can be interrupted by signals */
	bool should_exit; /* Checked on preempt, or schedule() */
	long preempt_count; /* Task can be preempted when zero */
	void* stack; /* Base address of the stack */
	size_t stack_size;
	struct {
		struct context general; /* General purpose registers */
		void* thread_local; /* Unused for kernel threads */
		void* extended; /* SSE, AVX, etc.. */
	} ctx; /* For the task switcher, obviously */
	struct list_node proc_link; /* Link for proc->threads */
	struct list_node sleep_link; /* Link so the scheduler can wake up threads */
	struct list_node block_link; /* Link for things like mutexes/semaphores */
	struct list_node zombie_link; /* For reaper thread */
	void* policy_priv; /* For the scheduling algorithm */
	atomic(unsigned long) refcount;
};

/* Not needed (probably), but make 100% sure this is the case, assembly code expects this to be here */
static_assert(offsetof(struct thread, utk_stack_top) == 0, "offsetof(struct thread, utk_stack_top) == 0");

struct sched_policy;

struct runqueue {
	const struct sched_policy* policy;
	struct thread* current, *idle;
	struct list_head sleepers; /* Sleeping threads, may also contain blocked threads for timeouts */
	struct list_head zombies; /* For reaper thread */
	atomic(unsigned long) thread_count;
	void* policy_priv; /* For scheduling algorithm */
	spinlock_t lock, zombie_lock;
	struct semaphore reaper_sem;
};

void sched_cpu_init(void);
void sched_init(void);

void atomic_context_switch(struct thread* prev, struct thread* next, struct context* ctx);
struct thread* atomic_schedule(void);

static inline void thread_ref(struct thread* thread) {
	atomic_fetch_add_explicit(&thread->refcount, 1, ATOMIC_RELEASE);
}

static inline void thread_unref(struct thread* thread) {
	bug(atomic_fetch_sub_explicit(&thread->refcount, 1, ATOMIC_ACQ_REL) == 0);
}

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

int sched_add_to_proctbl(struct proc* proc);
int sched_get_from_proctbl(pid_t pid, struct proc** proc);
int sched_remove_proctbl(pid_t pid);
