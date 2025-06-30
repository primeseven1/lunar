#include <crescent/common.h>
#include <crescent/mm/slab.h>
#include <crescent/core/panic.h>
#include <crescent/sched/sched.h>
#include <crescent/sched/kthread.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/lib/string.h>
#include <crescent/asm/segment.h>
#include "sched.h"

static struct proc* kernel_proc = NULL;

static struct thread* find_thread(struct thread* thread) {
	while (thread) {
		if (__atomic_load_n(&thread->state, __ATOMIC_SEQ_CST) == THREAD_STATE_RUNNABLE)
			break;

		thread = thread->sched.next;
	}

	return thread;
}

void sched_switch(struct context* context) {
	struct cpu* cpu = current_cpu();

	spinlock_lock(&cpu->thread_queue_lock);

	struct thread* current = cpu->current_thread;
	struct thread* new_thread = find_thread(current);
	if (!new_thread) {
		new_thread = find_thread(cpu->thread_queue);
		if (!new_thread)
			goto out;
	}

	current->ctx.general_regs = *context;
	__atomic_store_n(&current->state, THREAD_STATE_RUNNABLE, __ATOMIC_SEQ_CST);

	const void* __asm_isr_common_ret = context->__asm_isr_common_ret;
	*context = new_thread->ctx.general_regs;
	context->__asm_isr_common_ret = __asm_isr_common_ret;

	__atomic_store_n(&new_thread->state, THREAD_STATE_RUNNING, __ATOMIC_SEQ_CST);
	cpu->current_thread = new_thread;
out:
	spinlock_unlock(&cpu->thread_queue_lock);
}

void sched_schedule(struct thread* thread, struct proc* proc) {
	if (!proc)
		proc = kernel_proc;

	struct cpu* cpu = current_cpu();

	thread->proc = proc;

	unsigned long flags;
	spinlock_lock_irq_save(&proc->thread_lock, &flags);
	spinlock_lock(&cpu->thread_queue_lock);

	/* Add thread to the process struct */
	struct thread* _thread = proc->threads;
	if (_thread) {
		while (_thread->next)
			_thread = _thread->next;
		_thread->next = thread;
	} else {
		proc->threads = thread;
	}
	__atomic_add_fetch(&proc->thread_count, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n(&thread->state, THREAD_STATE_RUNNABLE, __ATOMIC_SEQ_CST);

	/* Now add thread to the CPU run queue, the CPU must always be running at least 1 thread */
	_thread = cpu->current_thread;
	while (_thread->sched.next)
		_thread = _thread->sched.next;
	_thread->sched.next = thread;

	spinlock_unlock(&cpu->thread_queue_lock);
	spinlock_unlock_irq_restore(&proc->thread_lock, &flags);
}

static _Noreturn void* idle(void* arg) {
	(void)arg;
	while (1)
		printk("idle\n");
}

void sched_init(void) {
	sched_proc_init();
	sched_thread_init();

	struct proc* kproc = sched_proc_create();
	assert(kproc);
	assert(kproc->pid == 0);

	struct thread* this_thread = sched_thread_create();
	assert(this_thread != NULL);
	kproc->threads = this_thread;

	struct cpu* cpu = current_cpu();
	cpu->thread_queue = this_thread;
	cpu->current_thread = this_thread;
	kproc->vmm_ctx = &cpu->vmm_ctx;
	kernel_proc = kproc;

	struct thread* idle_thread = kthread_create(0, idle, NULL);
	assert(idle_thread != NULL);
	sched_timer_init();
}
