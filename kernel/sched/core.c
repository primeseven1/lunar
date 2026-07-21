#include <lunar/init.h>
#include <lunar/sched.h>
#include <lunar/proc.h>
#include <lunar/vmm.h>
#include <lunar/timer.h>
#include <lunar/irq.h>
#include <lunar/printk.h>
#include <arch/processor.h>
#include "internal.h"

int sched_thread_attach(struct thread* thread, struct proc* proc, int prio) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;
	int err = 0;

	atomic_store(&thread->state.state, THREAD_READY);
	proc_thread_attach(proc, thread);

	unsigned long flags;
	spinlock_acquire_irq_save(&rq->lock, &flags);

	if (rq->policy->ops->thread_attach)
		err = rq->policy->ops->thread_attach(rq, thread, prio);

	if (err == 0)
		atomic_fetch_add(&rq->thread_count, 1);
	else
		proc_thread_detach(thread);

	spinlock_release_irq_restore(&rq->lock, &flags);
	return err;
}

void sched_thread_detach(struct thread* thread) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;

	unsigned long flags;
	spinlock_acquire_irq_save(&rq->lock, &flags);

	if (rq->policy->ops->thread_detach)
		rq->policy->ops->thread_detach(rq, thread);
	proc_thread_detach(thread);
	atomic_fetch_sub(&rq->thread_count, 1);

	spinlock_release_irq_restore(&rq->lock, &flags);
}

static atomic(struct isr*) resched_isr = atomic_init(NULL);

static void resched_ipi(struct isr* isr) {
	(void)isr;
	current_cpu()->need_resched = true;
}

static void resched_timer_event(void* event_handle, void* arg) {
	(void)event_handle;
	(void)arg;
	current_cpu()->need_resched = true;
}

static void send_resched(struct cpu* cpu) {
	int err;

	/* The ISR being NULL means that there is only one CPU, so instead set a timer event to trigger right away */
	if (atomic_load(&resched_isr)) {
		err = irqctl_send_ipi(cpu, atomic_load(&resched_isr), 0);
	} else {
		const struct timer_event_handler handler = { .fn = resched_timer_event, .arg = NULL };
		err = arm_timer_event(0, &handler, TIMER_FLAG_EVENT_ALLOC_AUTOFREE | TIMER_FLAG_HARDIRQ, NULL);
	}

	if (unlikely(err))
		printk(PRINTK_CRIT "sched: Failed to send resched IPI: %i\n", err);
}

int sched_enqueue(struct thread* thread) {
	struct cpu* cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &cpu->runqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	bug(atomic_load(&thread->proc) == NULL);
	int ret = rq->policy->ops->enqueue(rq, thread);
	if (ret == 0 && atomic_load(&thread->prio) >= atomic_load(&atomic_load(&rq->current)->prio))
		send_resched(cpu);

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return ret;
}

int sched_dequeue(struct thread* thread) {
	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	bug(atomic_load(&thread->proc) == NULL);
	int ret = rq->policy->ops->dequeue(rq, thread);

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return ret;
}

int sched_change_prio(struct thread* thread, int prio) {
	struct cpu* cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &cpu->runqueue;
	if (!rq->policy->ops->change_prio)
		return -ENOSYS;

	if (prio < SCHED_PRIO_MIN)
		prio = SCHED_PRIO_MIN;
	if (prio > SCHED_PRIO_MAX)
		prio = SCHED_PRIO_MAX;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	int err = rq->policy->ops->change_prio(rq, thread, prio);
	if (err == 0) {
		atomic_store(&thread->prio, prio);
		if (prio >= atomic_load(&atomic_load(&rq->current)->prio))
			send_resched(cpu);
	}

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return err;
}

static bool __context_switch(struct runqueue* rq, struct thread* to) {
	struct thread* current = atomic_load(&rq->current);
	if (current == to)
		return false;

	spinlock_acquire(&rq->lock);

	int expected = THREAD_RUNNING;
	atomic_compare_exchange_strong(&current->state.state, &expected, THREAD_READY);
	expected = THREAD_READY;
	bug(atomic_compare_exchange_strong(&to->state.state, &expected, THREAD_RUNNING) == false);
	atomic_store(&rq->current, to);
	if (current->mm_struct != to->mm_struct)
		mm_switch_context(to->mm_struct);

	spinlock_release(&rq->lock);
	return true;
}

static void context_switch(struct thread* to) {
	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* current = atomic_load(&rq->current);
	bool switch_thread = __context_switch(rq, to);
	if (switch_thread)
		arch_context_switch(current, to);
}

static struct thread* __schedule(void) {
	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	struct thread* current = atomic_load(&rq->current);
	bug(current->preempt_count != 0);

	spinlock_acquire(&rq->lock);
	struct thread* next = rq->policy->ops->pick_next(rq);
	if (!next)
		next = rq->idle;
	if (next != current && current != rq->idle && rq->policy->ops->on_yield)
		rq->policy->ops->on_yield(rq, current);
	spinlock_release(&rq->lock);

	cpu->need_resched = false;
	return next;
}

struct thread* atomic_schedule(void) {
	struct thread* next = __schedule();
	return __context_switch(&current_cpu()->runqueue, next) ? next : NULL;
}

int schedule(void) {
	bug(local_irq_disabled(local_irq_read()) || in_interrupt());
	local_irq_disable();
	struct thread* next = __schedule();
	context_switch(next);
	int ret = atomic_load(&current_thread()->state.wakeup_errno);
	local_irq_enable();
	return ret;
}

int sched_yield(void) {
	schedule();
	return 0;
}

static int __sched_wakeup_locked(struct thread* thread, int wakeup_errno) {
	struct cpu* target_cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &target_cpu->runqueue;
	struct thread* rq_current = atomic_load(&rq->current);
	int expected = THREAD_SLEEPING;

	/* Here we need to wait for the CPU to reschedule, to avoid nasty race conditions */
	if (thread == rq_current) {
		if (target_cpu != current_cpu())
			return -EAGAIN;
		/*
		 * Indicates we are in an interrupt context, when this happens just set to running and return,
		 * since IRQ's should be off when handing the state of the current thread. This also prevents a deadlock
		 * where if you're trying to wake up the current thread on the current cpu, it waits infinitely for the
		 * current CPU to reschedule.
		 */
		atomic_compare_exchange_strong(&thread->state.state, &expected, THREAD_RUNNING);
		return 0;
	}

	if (!atomic_compare_exchange_strong(&thread->state.state, &expected, THREAD_READY))
		return 0;

	atomic_store(&thread->state.wakeup_errno, wakeup_errno);
	bug(rq->policy->ops->enqueue(rq, thread) != 0);
	if (atomic_load(&thread->prio) > atomic_load(&rq_current->prio))
		send_resched(target_cpu);
	return 0;
}

static int try_wakeup(struct thread* thread, int wakeup_errno, bool* send_ipi) {
	struct cpu* target_cpu = atomic_load(&thread->topology.cpu);
	struct runqueue* rq = &target_cpu->runqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	/* If the target CPU is the current one, the reschedule IPI will cause the CPU to reschedule right after enabling IRQ's */
	int err = __sched_wakeup_locked(thread, wakeup_errno);
	if (err == -EAGAIN && *send_ipi) {
		send_resched(target_cpu);
		*send_ipi = false;
	}

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	return err;
}

void sched_wakeup(struct thread* thread, int wakeup_errno) {
	bool send_ipi = true;
	int err;
	do {
		err = try_wakeup(thread, wakeup_errno, &send_ipi);
		arch_cpu_relax();
	} while (err == -EAGAIN);
	bug(err != 0);
}

struct sched_timer_arg {
	struct thread* thread;
	unsigned long long gen;
};

static struct slab_cache* atomic_sta_cache;

static void sched_timer_handler(void* event_handle, void* arg) {
	(void)event_handle;

	struct sched_timer_arg* targ = arg;
	struct thread* thread = targ->thread;

	struct runqueue* rq = &atomic_load(&thread->topology.cpu)->runqueue;

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&rq->lock, &irq_flags);

	if (atomic_load(&thread->state.sleep_gen) == targ->gen) {
		int errno = 0;
		if (atomic_load(&thread->state.flags) & THREAD_STATE_FLAG_TIMEOUT)
			errno = -ETIME;
		__sched_wakeup_locked(thread, errno);
	}

	spinlock_release_irq_restore(&rq->lock, &irq_flags);
	slab_cache_free(atomic_sta_cache, targ);
	free_timer_event_handle(event_handle);
}

int sched_prepare_sleep(time_t us, int flags) {
	bug(in_interrupt() == true);
	if (flags & THREAD_STATE_FLAG_TIMEOUT && us == 0)
		return -EINVAL;
	if (us < 0)
		return -EINVAL;

	struct thread* thread = current_thread();

	void* handle = NULL;
	struct sched_timer_arg* targ = NULL;
	if (us != 0) {
		/* Freed in an atomic context, and this function also may be called in an atomic context */
		handle = alloc_timer_event_handle(TIMER_FLAG_EVENT_ALLOC_ATOMIC);
		if (!handle)
			return -ENOMEM;
		targ = slab_cache_alloc(atomic_sta_cache);
		if (!targ) {
			free_timer_event_handle(handle);
			return -ENOMEM;
		}
	}

	int err = 0;
	unsigned long irq_flags = local_irq_save();

	unsigned long long gen = atomic_add_fetch(&thread->state.sleep_gen, 1);
	atomic_store(&thread->state.flags, flags);
	atomic_store(&thread->state.state, THREAD_SLEEPING);

	if (handle) {
		targ->thread = thread;
		targ->gen = gen;
		const struct timer_event_handler handler = { .fn = sched_timer_handler, .arg = targ };
		err = arm_timer_event_handle(handle, us, &handler, 0);
		if (err) {
			atomic_store(&thread->state.state, THREAD_RUNNING);
			free_timer_event_handle(handle);
			slab_cache_free(atomic_sta_cache, targ);
		}
	}

	local_irq_restore(irq_flags);
	return err;
}

_Noreturn void sched_thread_exit(void) {
	local_irq_disable();

	/*
	 * If a thread isn't running at the time this is called (eg. With a sched_prepare_sleep()),
	 * it's possible that the thread can continue executing in an unsafe place when woken up.
	 */
	struct thread* thread = current_thread();
	bug(atomic_exchange(&thread->state.state, THREAD_ZOMBIE) != THREAD_RUNNING);

	struct runqueue* rq = &current_cpu()->runqueue;

	spinlock_acquire(&rq->zombie_lock);
	list_add_tail(&rq->zombie_list, &thread->state.block_link);
	semaphore_signal(&rq->reaper_sem);
	spinlock_release(&rq->zombie_lock);

	local_irq_enable();
	schedule();
	bug("unreachable");
}

static struct proc* kernel_proc;

static struct thread* create_bootstrap_thread(void (*exec)(void), int state, int prio) {
	struct thread* thread = alloc_thread(SCHED_TOPOLOGY_CURRENT | SCHED_TOPOLOGY_NO_MIGRATE);
	if (!thread)
		out_of_memory();

	int err = alloc_thread_stack(thread, 0, NULL, NULL);
	if (err) {
		if (err == -ENOMEM)
			out_of_memory();
		else
			panic("Failed to allocate bootstrap thread stack: %d\n", err);
	}

	/*
	 * When priority is zero, it means that it's the idle thread.
	 * The idle thread does not get attached to a runqueue, so only attach it to the process.
	 * Not doing so will cause a null dereference when switching to the idle thread.
	 */
	if (prio) {
		err = sched_thread_attach(thread, kernel_proc, prio);
		if (err == -ENOMEM)
			out_of_memory();
		bug(err != 0);
	} else {
		proc_thread_attach(kernel_proc, thread);
	}
	atomic_store(&thread->state.state, state);

	const struct thread_entry_point entry_point = { .kernel_entry = exec, .user_entry = NULL };
	arch_thread_prepare_execution(thread, &entry_point);

	return thread;
}

static void idle(void) {
	while (1)
		arch_cpu_idle();
}

static atomic(u32) sched_id_counter = atomic_init(0);

void sched_assign_id(void) {
	u32 id = atomic_fetch_add_explicit(&sched_id_counter, 1, ATOMIC_RELAXED);
	bug(id >= arch_get_cpu_count());
	current_cpu()->runqueue.sched_id = id;
}

static void sched_bootstrap_processor(void) {
	struct runqueue* rq = &current_cpu()->runqueue;

	spinlock_init(&rq->lock);
	list_head_init(&rq->zombie_list);
	spinlock_init(&rq->zombie_lock);
	semaphore_init(&rq->reaper_sem, 0);

	/* First create the current thread, and we don't need the ref alloc_thread() gives */
	struct thread* thread = create_bootstrap_thread(NULL, THREAD_RUNNING, SCHED_PRIO_DEFAULT);
	atomic_store(&rq->current, thread);
	THREAD_RELEASE(thread);

	/* Now create the idle thread, this thread should never be destroyed, so keep the ref */
	thread = create_bootstrap_thread(idle, THREAD_READY, 0);
	rq->idle = thread;
}

static void sched_init(void) {
	atomic_sta_cache = slab_cache_create(sizeof(struct sched_timer_arg), alignof(struct sched_timer_arg),
			MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
	if (!atomic_sta_cache)
		out_of_memory();

	sched_policy_cpu_init();
	sched_thread_cache_init();
	bug(proc_get(0, &kernel_proc) != 0);

	if (arch_get_cpu_count() > 1) {
		struct isr* isr = alloc_isr();
		if (!isr)
			out_of_memory();
		int err = register_isr(isr, resched_ipi, NULL, ISR_FLAG_TYPE_SGI);
		if (err)
			panic("Failed to create reschedule IPI");
		atomic_store(&resched_isr, isr);
	}

	sched_bootstrap_processor();
}

static void sched_ap_init(void) {
	sched_policy_cpu_init();
	sched_bootstrap_processor();
}

INIT_TASK_DECLARE(timers_init_task, timekeeper_init_task, timers_ap_init_task, timekeeper_ap_init_task, proc_init_task);
INIT_TASK_DEFINE(sched_init_task, INIT_TASK_SCOPE_BSP, sched_init, &timers_init_task, &timekeeper_init_task, &proc_init_task);
INIT_TASK_DEFINE(sched_ap_init_task, INIT_TASK_SCOPE_AP, sched_ap_init, &timers_ap_init_task, &timekeeper_ap_init_task);
