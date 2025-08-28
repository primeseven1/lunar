#include <crescent/compiler.h>
#include <crescent/asm/wrap.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/core/spinlock.h>
#include <crescent/sched/scheduler.h>
#include <crescent/sched/preempt.h>
#include <crescent/asm/errno.h>
#include "internal.h"

int sched_thread_attach(struct runqueue* rq, struct thread* thread, int prio) {
	size_t sz = rq->policy->thread_priv_size;
	if (unlikely(sz == 0))
		return 0;
	if (thread->attached)
		return 0;

	void* priv = kzalloc(sz, MM_ZONE_NORMAL);
	if (!priv)
		return -ENOMEM;

	atomic_store(&thread->state, THREAD_READY, ATOMIC_RELEASE);
	thread->policy_priv = priv;

	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	if (rq->policy->ops->thread_attach)
		rq->policy->ops->thread_attach(rq, thread, prio);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	thread->attached = true;
	return 0;
}

void sched_thread_detach(struct runqueue* rq, struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	if (rq->policy->ops->thread_detach)
		rq->policy->ops->thread_detach(rq, thread);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	size_t sz = rq->policy->thread_priv_size;
	if (sz == 0)
		return;
	if (thread->policy_priv) {
		kfree(thread->policy_priv);
		thread->policy_priv = NULL;
	}

	thread->attached = false;
}

int sched_enqueue(struct runqueue* rq, struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	assert(thread->attached);
	assert(rq->policy->ops->enqueue != NULL);
	int ret = rq->policy->ops->enqueue(rq, thread);

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return ret;
}

int sched_dequeue(struct runqueue* rq, struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	assert(thread->attached == true);
	assert(rq->policy->ops->dequeue != NULL);
	int ret = rq->policy->ops->dequeue(rq, thread);

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return ret;
}

struct thread* sched_pick_next(struct runqueue* rq) {
	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	struct thread* ret = rq->policy->ops->pick_next(rq);
	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return ret;
}

/* not implemented for multicore yet */
struct cpu* sched_decide_cpu(int flags) {
	if (flags & SCHED_CPU0) {
		u64 count;
		struct cpu** cpus = get_cpu_structs(&count);
		for (u64 i = 0; i < count; i++) {
			if (cpus[i]->processor_id == 0)
				return cpus[i];
		}
	}
	return current_cpu();
}

static int __sched_wakeup_locked(struct thread* thread, int wakeup_err) {
	if (wakeup_err != 0 && wakeup_err != -ETIMEDOUT && wakeup_err != -EINTR)
		return -EINVAL;
	int state = atomic_load(&thread->state, ATOMIC_ACQUIRE);
	if (state == THREAD_READY || state == THREAD_RUNNING)
		return 0;

	if (list_node_linked(&thread->sleep_link))
		list_remove(&thread->sleep_link);

	struct runqueue* rq = &thread->target_cpu->runqueue;

	atomic_store(&thread->wakeup_err, wakeup_err, ATOMIC_RELEASE);
	atomic_store(&thread->state, THREAD_READY, ATOMIC_RELEASE);
	assert(rq->policy->ops->enqueue(rq, thread) == 0);

	return 0;
}

int sched_wakeup(struct thread* thread, int wakeup_err) {
	struct runqueue* rq = &thread->target_cpu->runqueue;

	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	int ret = __sched_wakeup_locked(thread, wakeup_err);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	return ret;
}

void sched_tick(void) {
	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = rq->current;

	spinlock_lock(&rq->lock);

	if (rq->policy->ops->on_tick(rq, current))
		cpu->need_resched = true;
	else if (current == rq->idle)
		cpu->need_resched = true;

	time_t now = timekeeper_get_nsec();
	struct thread* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &rq->sleepers, sleep_link) {
		if (now >= pos->wakeup_time) {
			int err = 0;
			if (atomic_load(&pos->state, ATOMIC_ACQUIRE) == THREAD_BLOCKED)
				err = -ETIMEDOUT;
			__sched_wakeup_locked(pos, err);
			if (cpu->runqueue.current == cpu->runqueue.idle)
				cpu->need_resched = true;
		}
	}

	spinlock_unlock(&rq->lock);
}

int schedule(void) {
	unsigned long irq = local_irq_save();

	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;

	struct thread* prev = rq->current;
	if (prev->preempt_count) {
		assert(atomic_load(&prev->state, ATOMIC_ACQUIRE) == THREAD_RUNNING);
		return -EAGAIN;
	}

	struct thread* next = sched_pick_next(rq);
	if (!next) {
		if (atomic_load(&prev->state, ATOMIC_ACQUIRE) == THREAD_RUNNING)
			next = prev;
		else
			next = rq->idle;
	}

	if (prev == next) {
		cpu->need_resched = false;
		local_irq_restore(irq);
		return 0;
	}

	if (atomic_load(&prev->state, ATOMIC_ACQUIRE) == THREAD_RUNNING)
		atomic_store(&prev->state, THREAD_READY, ATOMIC_RELEASE);
	atomic_store(&next->state, THREAD_RUNNING, ATOMIC_RELEASE);

	rq->current = next;
	cpu->need_resched = false;
	context_switch(prev, next);

	int reason = atomic_load(&prev->wakeup_err, ATOMIC_ACQUIRE);
	local_irq_restore(irq);
	return reason;
}

int sched_yield(void) {
	unsigned long irq = local_irq_save();

	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = rq->current;

	assert(rq->policy->ops->on_yield);
	spinlock_lock(&rq->lock);
	rq->policy->ops->on_yield(rq, current);
	spinlock_unlock(&rq->lock);

	cpu->need_resched = false;
	schedule();

	local_irq_restore(irq);
	return 0;
}

void sched_prepare_sleep(time_t ms, int flags) {
	time_t sleep_end = 0;
	if (ms == 0 && !(flags & SCHED_SLEEP_BLOCK))
		return;
	else if (ms != 0)
		sleep_end = timekeeper_get_nsec() + (ms * 1000000);

	unsigned long irq = local_irq_save();

	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = rq->current;

	if (flags & SCHED_SLEEP_BLOCK)
		atomic_store(&current->state, THREAD_BLOCKED, ATOMIC_RELEASE);
	else
		atomic_store(&current->state, THREAD_SLEEPING, ATOMIC_RELEASE);
	if (flags & SCHED_SLEEP_INTERRUPTIBLE)
		atomic_store(&current->sleep_interruptable, true, ATOMIC_RELEASE);
	if (sleep_end) {
		assert(list_node_linked(&current->sleep_link) == false);
		current->wakeup_time = sleep_end;
		spinlock_lock(&rq->lock);
		list_add(&rq->sleepers, &current->sleep_link);
		spinlock_unlock(&rq->lock);
	}

	local_irq_restore(irq);
}

_Noreturn void sched_thread_exit(void) {
	local_irq_disable();

	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = rq->current;
	atomic_store(&current->state, THREAD_ZOMBIE, ATOMIC_RELEASE);

	spinlock_lock(&rq->lock);
	list_add(&rq->zombies, &current->zombie_link);
	spinlock_unlock(&rq->lock);

	local_irq_enable();

	schedule();
	__builtin_unreachable();
}

static struct proc* kproc;

static void idle_thread(void) {
	while (1)
		cpu_halt();
}

static void sched_bootstrap_processor(void) {
	struct runqueue* rq = &current_cpu()->runqueue;

	struct thread* current = thread_create(kproc, PAGE_SIZE);
	assert(current != NULL);
	thread_set_ring(current, THREAD_RING_KERNEL);
	thread_add_to_proc(kproc, current);
	sched_thread_attach(rq, current, 1);
	atomic_store(&current->state, THREAD_RUNNING, ATOMIC_RELEASE);
	rq->current = current;
	current->target_cpu = current_cpu();

	struct thread* idle = thread_create(kproc, PAGE_SIZE);
	assert(idle != NULL);
	thread_set_ring(idle, THREAD_RING_KERNEL);
	thread_set_exec(idle, idle_thread);
	sched_thread_attach(rq, idle, 0);
	atomic_store(&idle->state, THREAD_READY, ATOMIC_RELEASE);
	rq->idle = idle;
	current->target_cpu = current_cpu();

	list_head_init(&rq->sleepers);
	list_head_init(&rq->zombies);
}

void sched_init(void) {
	sched_policy_cpu_init();
	preempt_init();
	procthrd_init();
	ext_context_init();

	kproc = proc_create();
	assert(kproc != NULL);
	assert(kproc->pid == 0);
	kproc->mm_struct = current_cpu()->mm_struct;

	kthread_init(kproc);
	sched_bootstrap_processor();
	deferred_init();
}
