#include <crescent/core/cpu.h>
#include "internal.h"

struct thread* atomic_schedule(void) {
	struct cpu* cpu = current_cpu();
	struct runqueue* rq = &cpu->runqueue;
	spinlock_lock(&rq->lock);

	struct thread* current = rq->current;
	struct thread* next = rr_pick_next(rq);
	if (current == next) {
		spinlock_unlock(&rq->lock);
		return next;
	}

	if (atomic_load(&current->state, ATOMIC_ACQUIRE) == THREAD_RUNNING)
		rr_enqueue_thread(current);
	else if (atomic_load(&current->state, ATOMIC_ACQUIRE) == THREAD_ZOMBIE)
		list_add(&rq->zombie, &current->zombie_link);

	if (next != rq->idle)
		rr_dequeue_thread(next);

	atomic_store(&next->state, THREAD_RUNNING, ATOMIC_RELEASE);
	next->time_slice = PREEMPT_TICKS;

	if (current->proc != next->proc)
		vmm_switch_mm_struct(next->proc->mm_struct);

	spinlock_unlock(&rq->lock);
	return next;
}

/* not implemented for multicore yet */
static struct cpu* decide_cpu(int flags) {
	(void)flags;
	return current_cpu();
}

void schedule_thread(struct thread* thread, int flags) {
	thread->target_cpu = decide_cpu(flags);
	thread->time_slice = PREEMPT_TICKS;

	unsigned long irq;
	spinlock_lock_irq_save(&thread->target_cpu->runqueue.lock, &irq);

	atomic_add_fetch(&thread->target_cpu->runqueue.thread_count, 1, ATOMIC_RELEASE);
	rr_enqueue_thread(thread);

	spinlock_unlock_irq_restore(&thread->target_cpu->runqueue.lock, &irq);
}

static int yield(void) {
	local_irq_disable();

	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->runqueue.current;
	struct thread* next = atomic_schedule();
	cpu->need_resched = false;
	if (next != current)
		context_switch(next);

	local_irq_enable();
	return atomic_load(&current->wakeup_err, ATOMIC_ACQUIRE);
}

int schedule(void) {
	assert((read_cpu_flags() & CPU_FLAG_INTERRUPT) != 0);
	int ret = yield();
	return ret;
}

_Noreturn void thread_exit(void) {
	assert(read_cpu_flags() & CPU_FLAG_INTERRUPT);
	atomic_store(&current_cpu()->runqueue.current->state, THREAD_ZOMBIE, ATOMIC_RELEASE);
	schedule();
	panic("thread in zombie state running!");
}

void thread_block_noyield(bool interruptable) {
	assert((read_cpu_flags() & CPU_FLAG_INTERRUPT) == 0);
	local_irq_disable();

	struct cpu* cpu = current_cpu();
	struct thread* current_thread = cpu->runqueue.current;
	spinlock_lock(&cpu->runqueue.lock);

	atomic_store(&current_thread->interruptable, interruptable, ATOMIC_RELEASE);
	atomic_store(&current_thread->state, THREAD_BLOCKED, ATOMIC_RELEASE);
	list_add(&cpu->runqueue.blocked, &current_thread->blocked_link);

	spinlock_unlock(&cpu->runqueue.lock);
	local_irq_enable();
}

int thread_block(bool interruptable) {
	unsigned long irq = local_irq_save();
	thread_block_noyield(interruptable);
	local_irq_restore(irq);
	return schedule();
}

void thread_sleep_noyield(time_t ms, bool interruptable) {
	assert((read_cpu_flags() & CPU_FLAG_INTERRUPT) == 0);
	if (ms == 0 || ms < 0)
		return;

	local_irq_disable();

	struct cpu* cpu = current_cpu();
	struct thread* thread = cpu->runqueue.current;

	spinlock_lock(&cpu->runqueue.lock);
	assert(thread->preempt_count == 0);
	thread->wakeup_time = timekeeper_get_nsec() + (ms * 1000000);
	atomic_store(&thread->interruptable, interruptable, ATOMIC_RELEASE);
	atomic_store(&thread->state, THREAD_SLEEPING, ATOMIC_RELEASE);
	list_add_tail(&cpu->runqueue.sleeping, &thread->sleep_link);
	spinlock_unlock(&cpu->runqueue.lock);

	local_irq_enable();
}

int thread_sleep(time_t ms, bool interruptable) {
	assert((read_cpu_flags() & CPU_FLAG_INTERRUPT) != 0);
	preempt_disable();
	thread_sleep_noyield(ms, interruptable);
	int ret = yield();
	preempt_enable();
	return ret;
}

int thread_wakeup(struct thread* thread, int errno) {
	if (errno != -EINTR && errno != -ETIMEDOUT && errno != 0)
		return -EINVAL;
	if (errno == -EINTR && !atomic_load(&thread->interruptable, ATOMIC_ACQUIRE))
		return -ERESTART;

	int state = atomic_load(&thread->state, ATOMIC_ACQUIRE);
	if (state != THREAD_SLEEPING && state != THREAD_BLOCKED)
		return 0;

	unsigned long irq = local_irq_save();
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->runqueue.lock);

	if (list_node_in_list(&thread->sleep_link))
		list_remove(&thread->sleep_link);
	else if (list_node_in_list(&thread->blocked_link))
		list_remove(&thread->blocked_link);
	atomic_store(&thread->wakeup_err, errno, ATOMIC_RELEASE);
	rr_enqueue_thread(thread);

	spinlock_unlock(&cpu->runqueue.lock);
	local_irq_restore(irq);

	return 0;
}
