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

struct thread {
	tid_t tid; /* Not used yet */
	struct proc* proc; /* What process controls this thread? */
	atomic(int) state; /* Running? Stopped? Dead? */
	atomic(unsigned long) refcount; /* When 0, the scheduler can destroy this thread */
	struct {
		struct thread* next; /* Points to a CPU specific thread */
	} sched; /* Managed by the scheduler */
	struct {
		u8* affinity; /* What processors can this thread run on? (unused for now) */
		void* stack_top;
		size_t stack_size;
	} info;
	struct {
		struct context general_regs; /* General purpose registers */
		__attribute__((aligned(16))) u8 fxsave[512]; /* x87/MMX/SSE state */
	} ctx;
	struct thread* next; /* Points to the next thread in the process */
};

struct proc {
	pid_t pid; /* Not used yet */
	struct thread* threads; /* Only the first thread is guaranteed to be associated with the process */
	atomic(unsigned long) thread_count; /* Number of threads for the process */
	spinlock_t thread_lock; /* Protects the linked list for the specific process */
	struct vmm_ctx* vmm_ctx; /* Virtual memory context */
	struct proc* parent, *sibling, *child, *next;
};
