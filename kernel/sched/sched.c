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

static inline void sleep_state_reset(struct thread* thread) {
	if (thread_state_get(thread) != THREAD_STATE_SLEEPING)
		return;
	time_t current = timekeeper_get_nsec();
	if (current >= thread->sleep_end_timestamp_ns)
		thread_state_set(thread, THREAD_STATE_READY);
}

static struct thread* __get_next_thread_per_cpu(struct thread* start) {
	struct cpu* cpu = current_cpu();
	if (start) {
		list_for_each_entry_cont(start, &cpu->thread_queue, queue_link) {
			sleep_state_reset(start);
			if (thread_state_get(start) == THREAD_STATE_READY)
				return start;
		}
	}

	list_for_each_entry(start, &cpu->thread_queue, queue_link) {
		sleep_state_reset(start);
		if (thread_state_get(start) == THREAD_STATE_READY)
			return start;
	}

	if (thread_state_get(cpu->current_thread) == THREAD_STATE_RUNNING)
		return cpu->current_thread;
	return NULL;
}

struct thread* get_next_thread(void) {
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->thread_lock);

	struct list_node* next_node = cpu->current_thread->queue_link.next;
	struct thread* next_thread;
	if (list_is_tail(&cpu->thread_queue, next_node))
		next_thread = NULL;
	else
		next_thread = list_entry(next_node, struct thread, queue_link);

	struct thread* ret = __get_next_thread_per_cpu(next_thread);
	spinlock_unlock(&cpu->thread_lock);
	return ret;
}

static void thread_register(struct cpu* cpu, struct thread* thread) {
	spinlock_lock(&cpu->thread_lock);
	list_add_tail(&cpu->thread_queue, &thread->queue_link);
	spinlock_unlock(&cpu->thread_lock);

	spinlock_lock(&thread->proc->thread_lock);
	list_add(&cpu->thread_queue, &thread->proc_link);
	atomic_add_fetch(&thread->proc->thread_count, 1, ATOMIC_RELEASE);
	spinlock_unlock(&thread->proc->thread_lock);
}

static int thread_init(struct thread* thread, struct proc* proc) {
	thread->proc = proc;
	thread_state_set(thread, THREAD_STATE_READY);
	return 0;
}

int schedule_thread(struct thread* thread, struct proc* proc, int flags) {
	(void)flags;
	if (!proc)
		proc = kernel_proc;
	int err = thread_init(thread, proc);
	if (err)
		return err;

	unsigned long irq_flags = local_irq_save();
	thread_register(current_cpu(), thread);
	local_irq_restore(irq_flags);
	return err;
}

static void switch_thread(struct thread* next) {
	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->current_thread;

	if (thread_state_get(current) == THREAD_STATE_RUNNING)
		thread_state_set(current, THREAD_STATE_READY);
	thread_state_set(next, THREAD_STATE_RUNNING);
	cpu->current_thread = next;
	if (current->proc != next->proc)
		vmm_switch_mm_struct(next->proc->mm_struct);

	/* Once this returns (if it does), we are running the current thread again */
	asm_context_switch(&current->ctx.general, &next->ctx.general);

	if (current->proc != next->proc)
		vmm_switch_mm_struct(current->proc->mm_struct);
	thread_state_set(current, THREAD_STATE_RUNNING);
	if (thread_state_get(next) == THREAD_STATE_RUNNING)
		thread_state_set(next, THREAD_STATE_READY);
	cpu->current_thread = current;
}

void schedule(void) {
	unsigned long flags = local_irq_save();
	assert((flags & CPU_FLAG_INTERRUPT) != 0);

	struct thread* next = get_next_thread();
	if (!next)
		panic("no runnable threads!");
	switch_thread(next);

	local_irq_enable();
}

void schedule_sleep(time_t ms) {
	unsigned long flags = local_irq_save();
	assert((flags & CPU_FLAG_INTERRUPT) != 0); /* call timekeeper_stall instead */

	struct thread* current_thread = current_cpu()->current_thread;
	thread_state_set(current_thread, THREAD_STATE_SLEEPING);
	current_thread->sleep_end_timestamp_ns = timekeeper_get_nsec() + (ms * 1000000);

	local_irq_enable();
	schedule();
}

static _Noreturn void* idle(void* arg) {
	(void)arg;
	while (1)
		__asm__ volatile("hlt");
}

void scheduler_init(void) {
	sched_create_init();
	preempt_init();

	kernel_proc = sched_proc_alloc();
	assert(kernel_proc != NULL);
	assert(kernel_proc->pid == 0);
	kernel_proc->mm_struct = current_cpu()->mm_struct;
	list_head_init(&kernel_proc->threads);

	struct cpu* cpu = current_cpu();
	list_head_init(&cpu->thread_queue);
	struct thread* this_thread = sched_thread_alloc();
	assert(this_thread != NULL);
	assert(schedule_thread(this_thread, kernel_proc, SCHED_THIS_CPU) == 0);
	thread_state_set(this_thread, THREAD_STATE_RUNNING);
	cpu->current_thread = this_thread;

	struct thread* idle_thread = kthread_create(0, 0, idle, NULL);
	assert(idle_thread != NULL);
}
