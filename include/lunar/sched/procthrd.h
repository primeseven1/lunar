#pragma once

#include <lunar/common.h>
#include <lunar/core/abi.h>
#include <lunar/core/cred.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/panic.h>
#include <lunar/mm/mm.h>
#include <lunar/lib/list.h>
#include <lunar/sched/topology.h>

enum thread_states {
	THREAD_NEW,
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_BLOCKED,
	THREAD_SLEEPING,
	THREAD_ZOMBIE
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
	void* utk_stack; /* User to kernel mode switch, RSP0 and syscall entry. Syscall entry expects this to be at offset 0 */
	size_t utk_stack_size; /* Size of the stack including the guard page */
	tid_t id; /* Thread ID */
	struct topology topology; /* What CPU's this thread can run on */
	atomic(bool) attached; /* Attached to the policy? */
	struct proc* proc; /* The process struct this thread is linked to */
	bool in_usercopy; /* Is the thread reading from or writing to a user pointer? */
	int prio; /* Priority of the current thread */
	atomic(int) state; /* ready, blocked, running, etc.. */
	time_t wakeup_time; /* When the thread should wake up in nanoseconds */
	atomic(int) wakeup_err; /* Wakeup error code (eg. -ETIMEDOUT, -EINTR)*/
	atomic(bool) sleep_interruptable; /* Can be interrupted by signals */
	atomic(bool) should_exit; /* Checked on preempt, or schedule() */
	long preempt_count; /* Task can be preempted when zero */
	void* kstack; /* Kernel thread stack. NULL when a user thread */
	void __user* ustack; /* User thread stack, NULL when a kernel thread */
	size_t stack_size; /* Size of either kstack or ustack, including guard page */
	struct {
		struct context general; /* General purpose registers */
		void* fsbase, *gsbase; /* FSBASE/GSBASE MSR's */
		void* extended; /* SSE, AVX, etc.. */
	} ctx; /* For the task switcher */
	struct list_node proc_link; /* Link for proc->threads */
	struct list_node sleep_link; /* Link so the scheduler can wake up threads */
	struct list_node block_link; /* Link for things like mutexes/semaphores */
	struct list_node zombie_link; /* For reaper thread */
	atomic(void*) policy_priv; /* For the scheduling algorithm */
	atomic(unsigned long) refcnt;
};

static inline void thread_ref(struct thread* thread) {
	atomic_fetch_add_explicit(&thread->refcnt, 1, ATOMIC_RELEASE);
}

static inline void thread_unref(struct thread* thread) {
	bug(atomic_fetch_sub_explicit(&thread->refcnt, 1, ATOMIC_ACQ_REL) == 0);
}

static_assert(offsetof(struct thread, utk_stack) == 0, "offsetof(struct thread, utk_stack_top) == 0");
