#include <crescent/sched/scheduler.h>
#include <crescent/asm/segment.h>
#include <crescent/mm/mm.h>
#include <crescent/core/cpu.h>
#include "internal.h"

static struct proc* kernel_proc = NULL;

static void this_thread_init(struct cpu* cpu) {
	struct thread* this_thread = thread_create(kernel_proc, 0);
	assert(this_thread != NULL);
	thread_set_ring(this_thread, THREAD_RING_KERNEL);
	atomic_store(&this_thread->state, THREAD_RUNNING, ATOMIC_RELAXED);
	this_thread->target_cpu = cpu;
	cpu->runqueue.current = this_thread;
}

static _Noreturn void idle(void) {
	while (1)
		__asm__ volatile("hlt");
}

static void idle_thread_init(struct cpu* cpu) {
	/* Idle threads do nothing, so just allocate one page */
	struct thread* idle_thread = thread_create(kernel_proc, PAGE_SIZE);
	assert(idle_thread != NULL);

	atomic_store(&idle_thread->state, THREAD_READY, ATOMIC_RELAXED);
	thread_set_exec(idle_thread, idle);
	thread_set_ring(idle_thread, THREAD_RING_KERNEL);

	idle_thread->target_cpu = cpu;
	cpu->runqueue.idle = idle_thread;
}

void scheduler_init_cpu(void) {
	struct cpu* cpu = current_cpu();
	list_head_init(&cpu->runqueue.queue);
	list_head_init(&cpu->runqueue.sleeping);
	list_head_init(&cpu->runqueue.zombie);
	list_head_init(&cpu->runqueue.blocked);
	this_thread_init(cpu);
	idle_thread_init(cpu);
}

void scheduler_init(void) {
	preempt_init();
	ext_context_init();
	proc_thread_alloc_init();

	kernel_proc = proc_create();
	assert(kernel_proc != NULL);
	assert(kernel_proc->pid == 0);
	kernel_proc->mm_struct = current_cpu()->mm_struct;
	kthread_init(kernel_proc);

	scheduler_init_cpu();
	deferred_init();
}
