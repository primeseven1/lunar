#pragma once

#include <lunar/atomic.h>
#include <lunar/list.h>
#include <lunar/mm.h>
#include <lunar/panic.h>
#include <lunar/smp.h>
#include <lunar/cred.h>

#include <arch/posix.h>
#include <arch/context.h>

#define THREAD_NEW 0
#define THREAD_RUNNING 1
#define THREAD_READY 2
#define THREAD_SLEEPING 3
#define THREAD_ZOMBIE 4
#define THREAD_DEAD 5

#define THREAD_STATE_FLAG_INTERRUPTIBLE (1 << 0)
#define THREAD_STATE_FLAG_TIMEOUT (1 << 1)

#define SCHED_PRIO_MIN 1
#define SCHED_PRIO_DEFAULT 45
#define SCHED_PRIO_MAX 99

/* Avoid recursive includes */
struct cpu;
struct sched_policy;
struct proc;

struct context {
	struct arch_context arch_context;
	struct arch_context_extended arch_extended_context;
};

struct thread {
	struct {
		atomic(struct cpu*) cpu; /* CPU this thread is running on */
		struct cpumask cpumask; /* CPU's this thread is allowed to run on */
		atomic(bool) migratable; /* Can this thread move to another CPU? */
	} topology;
	atomic(struct proc*) proc; /* Process this thread is associated with */
	struct list_node proc_link; /* For thread list in process struct */
	struct {
		struct context ctx; /* CPU registers */
		void* stack_base; /* Base of the stack */
		size_t stack_size; /* Size of the stack including the guard page */
	} context;
	struct {
		atomic(int) state, flags; /* Running, sleeping, interruptible, etc. */
		atomic(int) wakeup_errno; /* The reason for waking up the thread (eg. -EINTR)*/
		atomic(unsigned long long) sleep_gen; /* Bumped on each sleep to prevent stale wakeups */
		struct list_node block_link; /* Used by things like semaphores to manage sleeping threads */
	} state;
	atomic(int) prio; /* Scheduler priority */
	long preempt_count; /* If zero, the thread can be preempted */
	atomic(unsigned long) refcnt;
	atomic(void*) policy_priv;
};

struct proc {
	pid_t pid;
	struct cred* cred;
	struct mm* mm_struct;
	struct list_head thread_list;
	atomic(unsigned int) thread_count;
	spinlock_t lock;
	struct {
		struct vnode* cwd, *root;
		mutex_t mtx;
	} fs;
};

static inline void thread_ref(struct thread* thread) {
	atomic_fetch_add(&thread->refcnt, 1);
}

static inline void thread_unref(struct thread* thread) {
	bug(atomic_fetch_sub(&thread->refcnt, 1) == 0);
}

struct thread* sched_thread_alloc(int flags);
void sched_thread_destroy(struct thread* thread);
struct proc* sched_proc_alloc(void);
void sched_proc_destroy(struct proc* proc);
