#include <crescent/common.h>
#include <crescent/sched/sched.h>
#include <crescent/sched/kthread.h>
#include <crescent/mm/slab.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/core/printk.h>
#include <crescent/core/trace.h>
#include "sched.h"

static struct proc* kernel_proc;

static inline struct thread* __get_next_thread(struct thread* start) {
	while (start) {
		if (atomic_load(&start->state, ATOMIC_ACQUIRE) == THREAD_STATE_RUNNABLE)
			break;
		start = start->next;
	}

	return start;
}

/* Must be very simple, can be called from a task preemption */
struct thread* get_next_thread(void) {
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->thread_lock);

	struct thread* current = cpu->current_thread;
	assert(current != NULL);
	struct thread* ret = current->next;

	ret = __get_next_thread(ret);
	if (!ret && current != cpu->thread_queue)
		ret = __get_next_thread(cpu->thread_queue);

	spinlock_unlock(&cpu->thread_lock);
	return ret;
}

static void thread_add_to_queue(struct thread* thread) {
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->thread_lock);

	if (cpu->thread_queue) {
		struct thread* tail = cpu->thread_queue;
		while (tail->next)
			tail = tail->next;
		tail->next = thread;
		thread->prev = tail;
		thread->next = NULL;
	} else {
		cpu->thread_queue = thread;
		thread->prev = NULL;
		thread->next = NULL;
	}

	atomic_add_fetch(&thread->proc->thread_count, 1, ATOMIC_RELEASE);
	spinlock_unlock(&cpu->thread_lock);
}

static int new_thread_init(struct thread* thread, struct proc* proc, int flags) {
	(void)flags;
	thread->proc = proc;
	atomic_store(&thread->state, THREAD_STATE_RUNNABLE, ATOMIC_RELEASE);
	return 0;
}

int schedule_thread(struct thread* thread, struct proc* proc, int flags) {
	if (!proc)
		proc = kernel_proc;

	unsigned long irq_flags = local_irq_save();
	int err = new_thread_init(thread, proc, flags);
	if (err)
		goto out;

	thread_add_to_queue(thread);
out:
	local_irq_restore(irq_flags);
	return err;
}

static void switch_thread(struct thread* next) {
	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->current_thread;

	if (atomic_load(&current->state, ATOMIC_ACQUIRE) == THREAD_STATE_RUNNING)
		atomic_store(&current->state, THREAD_STATE_RUNNABLE, ATOMIC_RELEASE);
	atomic_store(&next->state, THREAD_STATE_RUNNING, ATOMIC_RELEASE);
	cpu->current_thread = next;
	if (current->proc != next->proc)
		vmm_switch_mm_struct(next->proc->mm_struct);

	/* Once this returns (if it does), we are running the current thread again */
	asm_context_switch(&current->ctx.general, &next->ctx.general);

	if (current->proc != next->proc)
		vmm_switch_mm_struct(current->proc->mm_struct);
	atomic_store(&current->state, THREAD_STATE_RUNNING, ATOMIC_RELEASE);
	if (atomic_load(&next->state, ATOMIC_ACQUIRE) == THREAD_STATE_RUNNING)
		atomic_store(&next->state, THREAD_STATE_RUNNABLE, ATOMIC_RELEASE);
	cpu->current_thread = current;
}

void sched_yield(void) {
	unsigned long flags = local_irq_save();
	if (!(flags & CPU_FLAG_INTERRUPT))
		panic("sched_yield called with interrupts disabled");

	struct thread* next = get_next_thread();
	if (!next)
		panic("no runnable threads!");
	switch_thread(next);

	local_irq_enable();
}

static _Noreturn void* idle(void* arg) {
	(void)arg;
	while (1)
		__asm__ volatile("hlt");
}

void sched_init(void) {
	sched_create_init();
	preempt_init();

	kernel_proc = sched_proc_alloc();
	assert(kernel_proc != NULL);
	assert(kernel_proc->pid == 0);
	kernel_proc->mm_struct = current_cpu()->mm_struct;

	struct thread* this_thread = sched_thread_alloc();
	assert(this_thread != NULL);
	assert(schedule_thread(this_thread, kernel_proc, 0) == 0);
	atomic_store(&this_thread->state, THREAD_STATE_RUNNING, ATOMIC_RELAXED);

	struct cpu* cpu = current_cpu();
	cpu->current_thread = this_thread;

	struct thread* idle_thread = kthread_create(0, 0, idle, NULL);
	assert(idle_thread != NULL);
}
