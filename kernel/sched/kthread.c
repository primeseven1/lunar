#include <crescent/sched/scheduler.h>
#include <crescent/sched/kthread.h>
#include <crescent/asm/segment.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/cpu.h>
#include <crescent/core/trace.h>
#include <crescent/core/panic.h>
#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/asm/wrap.h>
#include <crescent/asm/flags.h>
#include <crescent/sched/preempt.h>
#include "internal.h"

static struct proc* kproc;

struct thread* kthread_create(int sched_flags, void* (*func)(void*), void* arg) {
	struct thread* thread = thread_create(kproc, KSTACK_SIZE);
	if (!thread)
		return NULL;

	thread_set_ring(thread, THREAD_RING_KERNEL);
	thread_set_exec(thread, asm_kthread_start);
	if (sched_flags & SCHED_THIS_CPU)
		thread->target_cpu = sched_decide_cpu(sched_flags);

	int err = sched_thread_attach(&thread->target_cpu->runqueue, thread, SCHED_PRIO_DEFAULT);
	if (err)
		thread_destroy(thread);

	atomic_add_fetch(&thread->refcount, 1, ATOMIC_ACQUIRE);
	thread->ctx.general.rdi = (uintptr_t)func;
	thread->ctx.general.rsi = (uintptr_t)arg;

	err = sched_enqueue(&thread->target_cpu->runqueue, thread);
	if (unlikely(err)) {
		sched_thread_detach(&thread->target_cpu->runqueue, thread);
		thread_destroy(thread);
	}

	return thread;
}

void* kthread_join(struct thread* thread) {
	while (atomic_load(&thread->state, ATOMIC_ACQUIRE) != THREAD_ZOMBIE)
		schedule();

	void* ret = (void*)thread->ctx.general.rax;
	atomic_sub_fetch(&thread->refcount, 1, ATOMIC_RELEASE);
	return ret;
}

_Noreturn void kthread_exit(void* ret) {
	struct runqueue* rq = &current_cpu()->runqueue;
	struct thread* thread = rq->current;
	thread->ctx.general.rax = (long)ret;
	sched_thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage _Noreturn void __kthread_start(void* (*func)(void*), void* arg) {
	void* ret = func(arg);
	printk(PRINTK_WARN "function at %p failed to call kthread_exit!\n", func);
	kthread_exit(ret);
}

__diag_pop();

void kthread_init(struct proc* kernel_proc) {
	kproc = kernel_proc;
}
