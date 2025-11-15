#include <lunar/compiler.h>
#include <lunar/asm/wrap.h>
#include <lunar/asm/errno.h>
#include <lunar/mm/heap.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/cpu.h>
#include <lunar/core/printk.h>
#include <lunar/core/apic.h>
#include <lunar/core/time.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/preempt.h>
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

	irqflags_t irq;
	spinlock_lock_irq_save(&rq->lock, &irq);
	if (rq->policy->ops->thread_attach)
		rq->policy->ops->thread_attach(rq, thread, prio);
	atomic_add_fetch(&rq->thread_count, 1);
	spinlock_unlock_irq_restore(&rq->lock, &irq);

	thread->attached = true;
	return 0;
}

void sched_thread_detach(struct runqueue* rq, struct thread* thread) {
	irqflags_t irq;
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
	irqflags_t irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	assert(thread->attached);
	assert(rq->policy->ops->enqueue != NULL);
	int ret = rq->policy->ops->enqueue(rq, thread);
	if (ret == 0 && thread->prio > rq->current->prio) {
		struct cpu* this_cpu = current_cpu();
		if (thread->target_cpu == this_cpu)
			this_cpu->need_resched = true;
		else
			sched_send_resched(thread->target_cpu);
	}

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return ret;
}

int sched_dequeue(struct runqueue* rq, struct thread* thread) {
	irqflags_t irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	assert(thread->attached == true);
	assert(rq->policy->ops->dequeue != NULL);
	int ret = rq->policy->ops->dequeue(rq, thread);

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return ret;
}

struct thread* sched_pick_next(struct runqueue* rq) {
	irqflags_t irq;
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

	irqflags_t irq;
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

	irqflags_t irq;
	spinlock_lock_irq_save(&rq->lock, &irq);

	int err = rq->policy->ops->change_prio(rq, thread, prio);
	if (likely(err == 0)) {
		thread->prio = prio;
		if (rq->current->prio > thread->prio) {
			struct cpu* this_cpu = current_cpu();
			if (thread->target_cpu == this_cpu)
				this_cpu->need_resched = true;
			else
				sched_send_resched(thread->target_cpu);
		}
	}

	spinlock_unlock_irq_restore(&rq->lock, &irq);
	return err;
}

void sched_tick(void) {
	irqflags_t irq_flags = local_irq_save();

	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = rq->current;

	spinlock_lock(&rq->lock);

	if (rq->policy->ops->on_tick(rq, current))
		cpu->need_resched = true;
	else if (current == rq->idle)
		cpu->need_resched = true;

	struct timespec ts_now = timekeeper_time(TIMEKEEPER_FROMBOOT);
	time_t now = timespec_to_ns(&ts_now);
	while (!list_empty(&rq->sleepers)) {
		struct thread* thread = list_first_entry(&rq->sleepers, struct thread, sleep_link);

		/* List is ordered to keep interrupt latency fast */
		if (now < thread->wakeup_time)
			break;

		/* Can happen if a timer interrupt triggers before the CPU can reschedule */
		if (unlikely(thread == current)) {
			cpu->need_resched = true;
			break;
		}

		int err = atomic_load(&thread->state) == THREAD_BLOCKED ? -ETIMEDOUT : 0;
		__sched_wakeup_locked(thread, err);
		if (current == cpu->runqueue.idle)
			cpu->need_resched = true;
	}

	spinlock_unlock(&rq->lock);
	local_irq_restore(irq_flags);
}

struct thread* atomic_schedule(void) {
	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;

	struct thread* prev = rq->current;
	if (prev->preempt_count) {
		bug(atomic_load(&prev->state) != THREAD_RUNNING);
		return NULL;
	}

	/* If there is no thread to run, see if the current thread is still runnable. If not, pick idle */
	struct thread* next = sched_pick_next(rq);
	if (!next) {
		if (atomic_load(&prev->state) == THREAD_RUNNING)
			next = prev;
		else
			next = rq->idle;
	}

	if (prev == next) {
		cpu->need_resched = false;
		return NULL;
	}

	/* If the state is modified (eg. by sched_thread_exit), then don't make the thread ready */
	int prev_state = atomic_load(&prev->state);
	if (prev_state == THREAD_RUNNING)
		atomic_store(&prev->state, THREAD_READY);
	else if (prev_state == THREAD_ZOMBIE)
		semaphore_signal(&rq->reaper_sem); /* Signal the current CPU's semaphore, safe to do since IRQ's are disabled */

	rq->current = next;
	cpu->current_thread = rq->current;
	cpu->need_resched = false;
	atomic_store(&next->state, THREAD_RUNNING);

	return next;
}

int schedule(void) {
	bug(in_interrupt() == true);
	irqflags_t irq = local_irq_save();

	struct thread* prev = current_cpu()->runqueue.current;
	struct thread* next = atomic_schedule();
	if (!next) {
		local_irq_restore(irq);
		return 0;
	}

	context_switch(prev, next);

	/* The reason is only valid if a sleep is prepared, otherwise an undefined value is returned */
	int reason = atomic_load(&prev->wakeup_err);
	local_irq_restore(irq);
	return reason;
}

int sched_yield(void) {
	irqflags_t irq = local_irq_save();

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

int sched_prepare_sleep(time_t ms, int flags) {
	bug(in_interrupt() == true);

	/* First determine the end of the sleep for the scheduler */
	time_t sleep_end = 0;
	if (!(flags & SCHED_SLEEP_BLOCK) && ms == 0)
		return -EINVAL;
	if (ms != 0) {
		struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
		sleep_end = timespec_to_ns(&ts) + (ms * 1000000);
	}

	irqflags_t irq = local_irq_save();

	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = rq->current;

	int prev_state = atomic_load(&current->state);
	bug(prev_state == THREAD_BLOCKED || prev_state == THREAD_SLEEPING ||
			list_node_linked(&current->sleep_link));

	atomic_store(&current->sleep_interruptable, (flags & SCHED_SLEEP_INTERRUPTIBLE) != 0);
	if (flags & SCHED_SLEEP_BLOCK)
		atomic_store(&current->state, THREAD_BLOCKED);
	else
		atomic_store(&current->state, THREAD_SLEEPING);

	/* Check if the thread is blocking with no timeout */
	if (sleep_end) {
		current->wakeup_time = sleep_end;
		spinlock_lock(&rq->lock);

		/* Now put the thread in order by wakeup time */
		struct thread* pos;
		bool inserted = false;
		list_for_each_entry(pos, &rq->sleepers, sleep_link) {
			if (current->wakeup_time < pos->wakeup_time) {
				list_add_before(&pos->sleep_link, &current->sleep_link);
				inserted = true;
				break;
			}
		}
		if (!inserted)
			list_add_tail(&rq->sleepers, &current->sleep_link);

		spinlock_unlock(&rq->lock);
	}

	local_irq_restore(irq);
	return 0;
}

_Noreturn void sched_thread_exit(void) {
	local_irq_disable();

	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = rq->current;

	/* 
	 * Can happen if the thread calls sched_prepare_sleep(), but then the thread calls sched_thread_exit().
	 * If that happens, the scheduler can start executing the thread again after schedule().
	 */
	bug(atomic_load(&current->state) != THREAD_RUNNING);

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
	struct thread* thread = thread_create(kproc, exec, PAGE_SIZE);
	if (!thread)
		panic("Failed to create a bootstrap thread\n");

	thread->target_cpu = current_cpu();
	sched_thread_attach(rq, thread, prio);
	atomic_store(&thread->state, state);

	return thread;
}

static void sched_bootstrap_processor(void) {
	struct runqueue* rq = &current_cpu()->runqueue;
	spinlock_init(&rq->lock);
	spinlock_init(&rq->zombie_lock);
	semaphore_init(&rq->reaper_sem, 0);

	struct thread* thread = create_bootstrap_thread(rq, NULL, THREAD_RUNNING, SCHED_PRIO_DEFAULT);
	rq->current = thread;
	current_cpu()->current_thread = rq->current;
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

static struct isr* resched_isr;

static void resched_ipi(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;

	struct thread* current = current_thread();
	if (current->preempt_count)
		return;
	current_cpu()->need_resched = true;
}

void sched_send_resched(struct cpu* target) {
	apic_send_ipi(target, resched_isr, APIC_IPI_CPU_TARGET, true);
}

void sched_init(void) {
	sched_policy_cpu_init();
	preempt_cpu_init();
	procthrd_init();
	ext_context_init();

	const struct cred kernel_cred = { .gid = 0, .uid = 0 };
	kproc = proc_create(&kernel_cred);
	if (!kproc)
		panic("Failed to create kernel process");
	bug(kproc->pid != KERNEL_PID);
	kproc->mm_struct = current_cpu()->mm_struct;

	kthread_init(kproc);
	sched_bootstrap_processor();
	workqueue_init();
	reaper_cpu_init();

	resched_isr = interrupt_alloc();
	if (unlikely(!resched_isr))
		panic("Failed to allocate resched ISR");
	interrupt_register(resched_isr, resched_ipi, apic_set_irq, -1, NULL, false);
}
