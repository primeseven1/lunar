#include <crescent/sched/scheduler.h>
#include <crescent/asm/segment.h>
#include <crescent/mm/mm.h>
#include <crescent/core/cpu.h>
#include "sched.h"

struct thread* __schedule_noswap(struct thread* current) {
	struct runqueue* rq = &current_cpu()->runqueue;
	spinlock_lock(&rq->lock);

	struct thread* next = rr_pick_next(rq);
	assert(next);
	if (current == next) {
		spinlock_unlock(&rq->lock);
		return next;
	}

	if (atomic_load(&current->state, ATOMIC_ACQUIRE) == THREAD_RUNNING) {
		atomic_store(&current->state, THREAD_READY, ATOMIC_RELEASE);
		rr_enqueue_thread(current);
	} else if (atomic_load(&current->state, ATOMIC_ACQUIRE) == THREAD_ZOMBIE) {
		list_add(&rq->zombie, &current->zombie_link);
	}

	if (next != rq->idle)
		rr_dequeue_thread(next);

	atomic_store(&next->state, THREAD_RUNNING, ATOMIC_RELEASE);
	next->time_slice = PREEMPT_TICKS;
	rq->current = next;

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
	atomic_store(&thread->state, THREAD_READY, ATOMIC_RELEASE);

	unsigned long irq;
	spinlock_lock_irq_save(&thread->target_cpu->runqueue.lock, &irq);
	rr_enqueue_thread(thread);
	spinlock_unlock_irq_restore(&thread->target_cpu->runqueue.lock, &irq);

	atomic_add_fetch(&thread->target_cpu->runqueue.thread_count, 1, ATOMIC_RELEASE);
}

void schedule(void) {
	unsigned long flags = local_irq_save();
	assert((flags & CPU_FLAG_INTERRUPT) != 0);

	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->runqueue.current;
	struct thread* next = __schedule_noswap(current);
	cpu->need_resched = false;
	if (next != current)
		asm_context_switch(&current->ctx.general, &next->ctx.general);

	local_irq_restore(flags);
}

void schedule_sleep(time_t ms) {
	if (ms == 0 || ms < 0)
		return;

	unsigned long irq = local_irq_save();
	struct cpu* cpu = current_cpu();
	struct thread* thread = cpu->runqueue.current;
	spinlock_lock(&cpu->runqueue.lock);

	assert(thread->preempt_count == 0);
	thread->wakeup_time = timekeeper_get_nsec() + (ms * 1000000);
	atomic_store(&thread->state, THREAD_SLEEPING, ATOMIC_RELEASE);
	list_add_tail(&cpu->runqueue.sleeping, &thread->sleep_link);

	spinlock_unlock(&cpu->runqueue.lock);
	local_irq_restore(irq);
	schedule();
}

void schedule_block_current_thread_noschedule(void) {
	unsigned long irq = local_irq_save();

	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->runqueue.current;
	atomic_store(&current->state, THREAD_BLOCKED, ATOMIC_RELEASE);

	spinlock_lock(&cpu->runqueue.lock);
	list_add_tail(&cpu->runqueue.blocked, &current->blocked_link);
	spinlock_unlock(&cpu->runqueue.lock);
	local_irq_restore(irq);
}

void schedule_unblock_thread(struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&thread->target_cpu->runqueue.lock, &irq);

	atomic_store(&thread->state, THREAD_READY, ATOMIC_RELEASE);
	list_remove(&thread->blocked_link);
	rr_enqueue_thread(thread);

	spinlock_unlock_irq_restore(&thread->target_cpu->runqueue.lock, &irq);
}

static _Noreturn void idle(void) {
	while (1)
		__asm__ volatile("hlt");
}

static struct proc* kernel_proc = NULL;

static struct thread* create_idle(void) {
	struct thread* idle_thread = thread_alloc();
	assert(idle_thread != NULL);

	idle_thread->target_cpu = current_cpu();
	idle_thread->time_slice = (time_t)-1;
	atomic_store(&idle_thread->state, THREAD_READY, ATOMIC_RELAXED);
	idle_thread->proc = kernel_proc;
	list_node_init(&idle_thread->queue_link);
	idle_thread->id = (tid_t)-1;
	idle_thread->preempt_count = LONG_MAX;
	idle_thread->stack = vmap_kstack();
	idle_thread->stack_size = KSTACK_SIZE;

	idle_thread->ctx.general.rflags = RFLAGS_DEFAULT;
	idle_thread->ctx.general.rip = idle;
	idle_thread->ctx.general.rsp = idle_thread->stack;
	idle_thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
	idle_thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
	assert(idle_thread->ctx.general.rsp != NULL);

	return idle_thread;
}

void scheduler_init_cpu(void) {
	struct cpu* cpu = current_cpu();

	list_head_init(&cpu->runqueue.queue);
	list_head_init(&cpu->runqueue.sleeping);
	list_head_init(&cpu->runqueue.zombie);
	list_head_init(&cpu->runqueue.blocked);

	struct thread* this_thread = thread_alloc();
	assert(this_thread != NULL);
	atomic_store(&this_thread->state, THREAD_RUNNING, ATOMIC_RELAXED);
	this_thread->target_cpu = cpu;
	this_thread->time_slice = PREEMPT_TICKS;
	this_thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
	this_thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
	this_thread->proc = kernel_proc;
	cpu->runqueue.current = this_thread;
	cpu->runqueue.idle = create_idle();
}

void scheduler_init(void) {
	preempt_init();
	proc_thread_alloc_init();

	kernel_proc = proc_alloc();
	assert(kernel_proc != NULL);
	assert(kernel_proc->pid == 0);
	kernel_proc->mm_struct = current_cpu()->mm_struct;
	list_head_init(&kernel_proc->threads);
	kthread_init(kernel_proc);

	scheduler_init_cpu();
	deferred_init();
}
