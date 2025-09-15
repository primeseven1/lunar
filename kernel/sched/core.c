#include <crescent/compiler.h>
#include <crescent/asm/wrap.h>
#include <crescent/core/cpu.h>
#include <crescent/mm/heap.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/printk.h>
#include <crescent/sched/scheduler.h>
#include <crescent/sched/preempt.h>
#include <crescent/asm/errno.h>
#include "crescent/core/time.h"
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

	atomic_store(&thread->state, THREAD_READY);
	assert(thread_attach_to_proc(thread) == 0);

	thread->policy_priv = priv;

	if (prio < SCHED_PRIO_MIN)
		prio = SCHED_PRIO_MIN;
	if (prio > SCHED_PRIO_MAX)
		prio = SCHED_PRIO_MAX;

	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	if (rq->policy->ops->thread_attach)
		rq->policy->ops->thread_attach(rq, thread, prio);
	atomic_add_fetch(&rq->thread_count, 1);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	thread->attached = true;
	return 0;
}

void sched_thread_detach(struct runqueue* rq, struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	if (rq->policy->ops->thread_detach)
		rq->policy->ops->thread_detach(rq, thread);
	atomic_sub_fetch(&rq->thread_count, 1);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	assert(thread_detach_from_proc(thread) == 0);
	size_t sz = rq->policy->thread_priv_size;
	thread->attached = false;
	if (sz == 0)
		return;
	if (thread->policy_priv) {
		kfree(thread->policy_priv);
		thread->policy_priv = NULL;
	}
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

struct cpu* sched_decide_cpu(int flags) {
	if (flags & SCHED_CPU0) {
		const struct smp_cpus* cpus = smp_cpus_get();
		for (u64 i = 0; i < cpus->count; i++) {
			if (cpus->cpus[i]->processor_id == 0)
				return cpus->cpus[i];
		}
	} else if (flags & SCHED_THIS_CPU) {
		return current_cpu();
	}

	const struct smp_cpus* smp_cpus = smp_cpus_get();
	struct cpu* best = current_cpu();
	unsigned long best_tc = atomic_load(&best->runqueue.thread_count);
	for (u32 i = 0; i < smp_cpus->count; i++) {
		struct cpu* current = smp_cpus->cpus[i];
		if (current == best)
			continue;
		unsigned long current_tc = atomic_load(&current->runqueue.thread_count);
		if (current_tc < best_tc) {
			best = current;
			best_tc = current_tc;
		}
	}

	return best;
}

static int __sched_wakeup_locked(struct thread* thread, int wakeup_err) {
	if (wakeup_err != 0 && wakeup_err != -ETIMEDOUT && wakeup_err != -EINTR)
		return -EINVAL;
	int state = atomic_load(&thread->state);
	if (state == THREAD_READY || state == THREAD_RUNNING)
		return 0;

	if (list_node_linked(&thread->sleep_link))
		list_remove(&thread->sleep_link);

	struct runqueue* rq = &thread->target_cpu->runqueue;

	atomic_store(&thread->wakeup_err, wakeup_err);
	atomic_store(&thread->state, THREAD_READY);
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

int sched_change_prio(struct thread* thread, int prio) {
	struct runqueue* rq = &thread->target_cpu->runqueue;
	if (!rq->policy->ops->change_prio)
		return -ENOSYS;

	if (prio < SCHED_PRIO_MIN)
		prio = SCHED_PRIO_MIN;
	if (prio > SCHED_PRIO_MAX)
		prio = SCHED_PRIO_MAX;

	unsigned long irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	int err = rq->policy->ops->change_prio(rq, thread, prio);
	if (likely(err == 0)) {
		thread->prio = prio;
		if (rq->current->prio > thread->prio) {
			struct cpu* this_cpu = current_cpu();
			if (thread->target_cpu == this_cpu)
				this_cpu->need_resched = true;
			/* TODO: Send resched IPI to other CPU */
		}
	}

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return err;
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

	struct timespec ts_now = timekeeper_time();
	time_t now = timespec_to_ns(&ts_now);
	struct thread* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &rq->sleepers, sleep_link) {
		if (now >= pos->wakeup_time) {
			int err = 0;
			if (atomic_load(&pos->state) == THREAD_BLOCKED)
				err = -ETIMEDOUT;
			__sched_wakeup_locked(pos, err);
			if (current == cpu->runqueue.idle)
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
		assert(atomic_load(&prev->state) == THREAD_RUNNING);
		return -EAGAIN;
	}

	struct thread* next = sched_pick_next(rq);
	if (!next) {
		if (atomic_load(&prev->state) == THREAD_RUNNING)
			next = prev;
		else
			next = rq->idle;
	}

	if (prev == next) {
		cpu->need_resched = false;
		local_irq_restore(irq);
		return 0;
	}

	int prev_state = atomic_load(&prev->state);
	if (prev_state == THREAD_RUNNING)
		atomic_store(&prev->state, THREAD_READY);
	else if (prev_state == THREAD_ZOMBIE)
		semaphore_signal(&rq->reaper_sem);
	atomic_store(&next->state, THREAD_RUNNING);

	rq->current = next;
	cpu->need_resched = false;
	context_switch(prev, next);

	int reason = atomic_load(&prev->wakeup_err);
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
	if (ms == 0 && !(flags & SCHED_SLEEP_BLOCK)) {
		return;
	} else if (ms != 0) {
		struct timespec ts = timekeeper_time();
		sleep_end = timespec_to_ns(&ts) + (ms * 1000000);
	}

	unsigned long irq = local_irq_save();

	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = rq->current;

	if (flags & SCHED_SLEEP_BLOCK)
		atomic_store(&current->state, THREAD_BLOCKED);
	else
		atomic_store(&current->state, THREAD_SLEEPING);
	if (flags & SCHED_SLEEP_INTERRUPTIBLE)
		atomic_store(&current->sleep_interruptable, true);
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
	atomic_store(&current->state, THREAD_ZOMBIE);

	spinlock_lock(&rq->zombie_lock);
	list_add(&rq->zombies, &current->zombie_link);
	spinlock_unlock(&rq->zombie_lock);

	local_irq_enable();
	schedule();
	__builtin_unreachable();
}

static struct proc* kproc;

static void idle_thread(void) {
	while (1)
		cpu_halt();
}

static struct thread* create_bootstrap_thread(struct runqueue* rq, void* exec, int state, int prio) {
	struct thread* thread = thread_create(kproc, PAGE_SIZE);
	if (!thread)
		panic("Failed to create a bootstrap thread\n");

	thread->target_cpu = current_cpu();
	atomic_store(&thread->state, state);
	thread_set_ring(thread, THREAD_RING_KERNEL);
	thread_set_exec(thread, exec);
	sched_thread_attach(rq, thread, prio);

	return thread;
}

static void sched_bootstrap_processor(void) {
	struct runqueue* rq = &current_cpu()->runqueue;
	spinlock_init(&rq->lock);
	spinlock_init(&rq->zombie_lock);
	semaphore_init(&rq->reaper_sem, 0);

	struct thread* thread = create_bootstrap_thread(rq, NULL, THREAD_RUNNING, SCHED_PRIO_DEFAULT);
	rq->current = thread;
	thread = create_bootstrap_thread(rq, idle_thread, THREAD_READY, SCHED_PRIO_MIN);
	rq->idle = thread;

	list_head_init(&rq->sleepers);
	list_head_init(&rq->zombies);
}

void sched_cpu_init(void) {
	sched_policy_cpu_init();
	preempt_cpu_init();
	ext_context_cpu_init();
	sched_bootstrap_processor();
	workqueue_cpu_init();
	reaper_cpu_init();
}

void sched_init(void) {
	sched_policy_cpu_init();
	preempt_cpu_init();
	procthrd_init();
	ext_context_init();

	kproc = proc_create();
	assert(kproc != NULL);
	assert(kproc->pid == 0);
	kproc->mm_struct = current_cpu()->mm_struct;

	kthread_init(kproc);
	sched_bootstrap_processor();
	workqueue_init();
	reaper_cpu_init();
}
