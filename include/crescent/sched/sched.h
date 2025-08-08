#pragma once

#include <crescent/core/spinlock.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/timekeeper.h>
#include <crescent/lib/list.h>

void scheduler_init(void);

enum thread_state {
	THREAD_STATE_RUNNING, /* currently executing */
	THREAD_STATE_READY, /* waiting for cpu time */
	THREAD_STATE_BLOCKED, /* waiting for i/o or a resource (eg. mutex) */
	THREAD_STATE_SLEEPING, /* delibrately suspended */
	THREAD_STATE_ZOMBIE /* finished execution, but not cleaned up (eg. waiting on a join) */
};

enum sched_flags {
	SCHED_THIS_CPU = (1 << 0),
	SCHED_CPU0 = (1 << 1)
};

/*Forward declarations to avoid recursive includes */
struct cpu;
struct mm;

typedef int pid_t;
typedef int tid_t;

struct proc;

struct thread {
	tid_t tid;
	struct cpu* target_cpu;
	struct proc* proc;
	atomic(int) state;
	bool interruptable;
	time_t sleep_end_timestamp_ns;
	void* stack;
	size_t stack_size;
	struct {
		struct context general;
		u8* extended;
	} ctx;
	struct list_node queue_link;
	struct list_node proc_link;
	struct list_node sleep_link;
	atomic(unsigned long) refcount;
};

static inline int thread_state_get(struct thread* thread) {
	return atomic_load(&thread->state, ATOMIC_ACQUIRE);
}

static inline void thread_state_set(struct thread* thread, int state) {
	atomic_store(&thread->state, state, ATOMIC_RELEASE);
}

struct proc {
	pid_t pid;
	struct mm* mm_struct;
	struct list_head threads;
	atomic(unsigned long) thread_count;
	spinlock_t thread_lock;
	struct proc* parent, *sibling, *child;
};

struct work {
	void (*work)(void* arg);
	void* arg;
	int flags;
};

void schedule(void);
void schedule_sleep(time_t ms);
void schedule_work(struct work* work);
