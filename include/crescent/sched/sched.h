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

/* Forward declarations to avoid recursive includes */
struct cpu;
struct mm;

typedef int pid_t;
typedef int tid_t;

struct proc;

struct thread {
	tid_t tid;
	struct proc* proc;
	atomic(int) state;
	atomic(unsigned long) refcount;
	void* stack;
	size_t stack_size;
	struct {
		struct context general;
		u8* extended;
	} ctx;
	struct thread* prev, *next;
};

struct proc {
	pid_t pid;
	struct mm* mm_struct;
	atomic(unsigned long) thread_count;
	struct proc* parent, *sibling, *child;
};

void sched_init(void);
void sched_yield(void);
