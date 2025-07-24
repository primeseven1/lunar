#pragma once

#include <crescent/core/locking.h>
#include <crescent/core/interrupt.h>

enum thread_states {
	THREAD_STATE_RUNNING,
	THREAD_STATE_RUNNABLE,
	THREAD_STATE_BLOCKED,
	THREAD_STATE_STOPPED,
	THREAD_STATE_ZOMBIE,
	THREAD_STATE_DEAD
};

enum sched_flags {
	SCHED_THIS_CPU = (1 << 0), /* Only run the thread on this CPU */
	SCHED_RUNNING = (1 << 1), /* Shouldn't be used under normal circumstances, used in initialization */
	SCHED_IDLE = (1 << 2) /* Shouldn't be used, used by the scheduler for initialization */
};

/* Forward declarations to avoid recursive includes */
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
	atomic(unsigned long) refcount;
	void* stack;
	size_t stack_size;
	struct {
		u8* affinity;
		struct thread* prev, *next;
		int flags;
	} sched_info;
	struct {
		struct thread* prev, *next;
	} proc_info;
	struct context ctx;
};

struct proc {
	pid_t pid;
	struct {
		unsigned long thread_count;
		struct thread* threads;
		spinlock_t lock;
	} threadinfo;
	struct mm* mm_struct;
	struct proc* parent, *sibling, *child;
};

void sched_init(void);
