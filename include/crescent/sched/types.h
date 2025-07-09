#pragma once

#include <crescent/core/interrupt.h>

typedef int pid_t;
typedef int tid_t;
struct proc;

enum thread_states {
	THREAD_STATE_RUNNING,
	THREAD_STATE_RUNNABLE,
	THREAD_STATE_STOPPED,
	THREAD_STATE_ZOMBIE,
	THREAD_STATE_DEAD
};

typedef struct thread {
	tid_t tid; /* Not used yet */
	struct cpu* target_cpu;
	struct proc* proc; /* What process controls this thread? */
	atomic(int) state; /* Atomic */
	atomic(unsigned long) refcount; /* When 0, the scheduler can destroy this thread, atomic */
	struct {
		struct thread* prev, *next; /* Points to the prev/next thread in the queue */
		u8* affinity; /* What processors can this thread run on? (unused for now) */
		unsigned int flags;
	} sched; /* Managed by the scheduler */
	struct {
		void* stack_top;
		size_t stack_size;
	} info;
	struct {
		struct context general_regs; /* General purpose registers */
		__attribute__((aligned(16))) u8 fxsave[512]; /* x87/MMX/SSE state */
	} ctx;
	struct thread* prev, *next; /* Points to the prev/next thread in the process */
} thread_t;

typedef struct proc {
	pid_t pid; /* Not used yet */
	struct {
		thread_t* threads;
		unsigned long thread_count;
		spinlock_t lock;
		u8* tid_map;
	} threadinfo;
	struct vmm_ctx* vmm_ctx;
	struct proc* parent, *sibling, *child;
} proc_t;
