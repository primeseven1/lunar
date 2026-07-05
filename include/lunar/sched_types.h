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

struct thread_stack {
	void* kernel_stack_top; /* Stack to switch to on syscalls and interrupts (unless it's a kthread) */
	size_t kernel_ptr_off; /* How many bytes already "consumed" */
	size_t kernel_size, kernel_guard_size;
	void __user* user_stack_top; /* Unused for kernel threads */
	size_t user_ptr_off; /* How many bytes already "consumed" */
	size_t user_size, user_guard_size;
};

struct topology {
	atomic(struct cpu*) cpu;
	struct cpumask cpumask;
	atomic(bool) migratable;
};

struct thread {
	struct thread_stack stack; /* The stack for the current thread, also describes what stack to switch to when entering the kernel */
	struct topology topology; /* What CPU's this thread can run on */
	struct mm* mm_struct; /* Might be different from proc->mm_struct */
	struct context context; /* Registers */
	atomic(struct proc*) proc; /* Process this thread is associated with */
	struct list_node proc_link; /* For thread list in process struct */
	atomic(int) prio; /* Scheduler priority */
	long preempt_count; /* If zero, the thread can be preempted */
	struct {
		atomic(int) state, flags; /* Running, sleeping, interruptible, etc. */
		atomic(int) wakeup_errno; /* The reason for waking up the thread (eg. -EINTR)*/
		atomic(unsigned long long) sleep_gen; /* Bumped on each sleep to prevent stale wakeups */
		struct list_node block_link; /* Used by things like semaphores to manage sleeping threads */
	} state;
	atomic(unsigned long) refcnt;
	atomic(void*) policy_priv;
};
static_assert(offsetof(struct thread, stack.kernel_stack_top) == 0);
static_assert(offsetof(struct thread, stack.kernel_ptr_off) == sizeof(void*) * 1);

struct thread_entry_point {
	void (*kernel_entry)(void);
	void (__user* user_entry)(void);
};

#define THREAD_HOLD(t) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(t), struct thread*), "__builtin_types_compatible_p(typeof(p), struct thread*)"); \
		atomic_fetch_add(&(t)->refcnt, 1); \
	} while (0)
#define THREAD_RELEASE(t) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(t), struct thread*), "__builtin_types_compatible_p(typeof(p), struct thread*)"); \
		atomic_fetch_sub(&(t)->refcnt, 1); \
	} while (0)

/**
 * @brief Allocate a thread structure
 *
 * Thread is returned with a ref
 *
 * @param flags SCHED_* flags
 * @return A pointer to the thread
 */
struct thread* sched_thread_alloc(int flags);

/**
 * @brief Destroy a thread
 * @param thread The thread to destroy
 */
void sched_thread_destroy(struct thread* thread);
