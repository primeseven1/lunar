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
#include "internal.h"

static struct proc* kproc;

struct thread* kthread_create(int sched_flags, void* (*func)(void*), void* arg) {
	struct thread* thread = thread_create(kproc, KSTACK_SIZE);
	if (!thread)
		return NULL;

	thread_set_ring(thread, THREAD_RING_KERNEL);
	thread_set_exec(thread, asm_kthread_start);

	thread->proc = kproc;
	thread->ctx.general.rdi = (long)func;
	thread->ctx.general.rsi = (long)arg;
	atomic_add_fetch(&thread->refcount, 1, ATOMIC_RELEASE);
	schedule_thread(thread, sched_flags);

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
	struct thread* thread = current_cpu()->runqueue.current;
	thread->ctx.general.rax = (long)ret;
	thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage _Noreturn void __kthread_start(void* (*func)(void*), void* arg) {
	/* It's preferred for the thread function to call kthread_exit for clarity, but it's not a huge deal if that doesn't happen */
	void* ret = func(arg);
	printk(PRINTK_WARN "function at %p failed to call kthread_exit!\n", func);
	kthread_exit(ret);
}

__diag_pop();

void kthread_init(struct proc* kernel_proc) {
	kproc = kernel_proc;
}
