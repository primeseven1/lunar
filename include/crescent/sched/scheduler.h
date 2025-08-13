#pragma once

#include <crescent/core/spinlock.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/timekeeper.h>
#include <crescent/lib/list.h>

struct cpu;
typedef int pid_t;
typedef int tid_t;

enum thread_states {
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
	pid_t pid;
	struct mm* mm_struct;
	u8* tid_map;
	struct list_head threads;
	atomic(unsigned long) thread_count;
	spinlock_t thread_lock;
	struct proc* parent, *sibling, *child;
};

struct thread {
	tid_t id;
	struct cpu* target_cpu;
	struct proc* proc;
	unsigned long cpu_mask;
	time_t time_slice, wakeup_time;
	atomic(int) state;
	void* stack;
	size_t stack_size;
	struct {
		struct context general;
		u8* extended;
	} ctx;
	long preempt_count;
	struct list_node queue_link; /* next in the CPU queue */
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

void schedule_thread(struct thread* thread, int flags);
int schedule_work(void (*fn)(void*), void* arg, int flags);
void deferred_init_cpu(void);

/**
 * @brief Swap to a new task but without actually swapping
 *
 * This function is used for primarily interrupt handlers where
 * the ISR entry restores the context for you.
 *
 * @param current The current task
 * @return The pointer to the next thread
 */
struct thread* __schedule_noswap(struct thread* current);
void schedule(void);
void schedule_sleep(time_t ms);

void schedule_block_current_thread_noschedule(void);
void schedule_unblock_thread(struct thread* thread);

void scheduler_init_cpu(void);
void scheduler_init(void);
