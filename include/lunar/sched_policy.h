#pragma once

#include <lunar/spinlock.h>
#include <lunar/list.h>
#include <lunar/sched_types.h>

struct sched_policy;

struct runqueue {
	u32 sched_id;
	const struct sched_policy* policy;
	atomic(struct thread*) current; /* Only changes under the lock AND by the CPU that owns the runqueue */
	struct thread* idle; /* For when there are no other threads to run */
	atomic(unsigned long) thread_count;
	struct list_head zombie_list;
	struct semaphore reaper_sem;
	spinlock_t zombie_lock;
	void* policy_priv; /* For scheduling algorithm */
	spinlock_t lock; /* When modifying runqueues, or changing the current thread */
};

struct sched_policy_ops {
	int (*init)(struct runqueue*); /* Initialize the runqueue */
	int (*thread_attach)(struct runqueue*, struct thread*, int); /* Attach a thread to a runqueue */
	void (*thread_detach)(struct runqueue*, struct thread*); /* Detach a thread from a runqueue */
	int (*enqueue)(struct runqueue*, struct thread*); /* Add a new thread to the queue. May be called from an atomic context */
	int (*dequeue)(struct runqueue*, struct thread*); /* Remove a thread from the queue. May be called from an interrupt context */
	struct thread* (*pick_next)(struct runqueue*); /* Add the current thread to the queue, and return a new one. Called in an atomic context */
	int (*change_prio)(struct runqueue*, struct thread*, int); /* Change the priority of a thread, returns -errno on failure */
	bool (*on_tick)(struct runqueue*, struct thread*); /* Happens on a timer interrupt, returns true if should reschedule */
	void (*on_yield)(struct runqueue*, struct thread*); /* Called when yielding (but not for sleeping/blocking) */
	unsigned long (*attached_refcount)(struct runqueue*, struct thread*); /* Returns how many references this policy holds when attached to a runqueue */
};

struct sched_policy {
	const char* name, *desc;
	const struct sched_policy_ops* ops;
};

#define __sched_policy __attribute__((section(".schedpolicies"), aligned(8), used))
