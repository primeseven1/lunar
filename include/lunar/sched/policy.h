#pragma once

#include <lunar/core/spinlock.h>
#include <lunar/core/semaphore.h>
#include <lunar/lib/list.h>

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

struct sched_policy_ops {
	int (*init)(struct runqueue*); /* Initialize the runqueue */
	void (*thread_attach)(struct runqueue*, struct thread*, int); /* Attach a thread to a runqueue. */
	void (*thread_detach)(struct runqueue*, struct thread*); /* Detach a thread from a runqueue. */
	int (*enqueue)(struct runqueue*, struct thread*); /* Add a new thread to the queue. May be called from an atomic context. */
	int (*dequeue)(struct runqueue*, struct thread*); /* Remove a thread from the queue. May be called from an interrupt context. */
	struct thread* (*pick_next)(struct runqueue*); /* Add the current thread to the queue, and return a new one. Called in an atomic context. */
	int (*change_prio)(struct runqueue*, struct thread*, int); /* Change the priority of a thread, returns -errno on failure. */
	bool (*on_tick)(struct runqueue*, struct thread*); /* Happens on a timer interrupt, returns true if should reschedule. */
	void (*on_yield)(struct runqueue*, struct thread*); /* Called when yielding (but not for sleeping/blocking). */
};

struct sched_policy {
	const char* name, *desc;
	const struct sched_policy_ops* ops;
	size_t thread_priv_size; /* Size of thread->policy_priv, allocated by the core */
};

#define __sched_policy __attribute__((section(".schedpolicies"), aligned(8), used))
