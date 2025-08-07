#include <crescent/sched/sched.h>
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
#include "sched.h"

/* Bit 1 is reserved, and must always be set */
#define RFLAGS_DEFAULT ((1 << 1) | CPU_FLAG_INTERRUPT)

struct thread* kthread_create(int sched_flags, int kthread_flags, void* (*func)(void*), void* arg) {
	struct thread* thread = sched_thread_alloc();
	if (!thread)
		return NULL;

	void* stack = vmap_kstack();
	if (!stack) {
		sched_thread_free(thread);
		return NULL;
	}
	thread->stack = stack;
	thread->stack_size = KSTACK_SIZE;

	thread->ctx.general.rip = asm_kthread_start;
	thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
	thread->ctx.general.rflags = RFLAGS_DEFAULT;
	thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
	thread->ctx.general.rsp = stack;
	thread->ctx.general.rdi = (long)func;
	thread->ctx.general.rsi = (long)arg;
	atomic_store(&thread->refcount, kthread_flags & KTHREAD_JOIN ? 1 : 0, ATOMIC_RELAXED);
	schedule_thread(thread, NULL, sched_flags);

	return thread;
}

void* kthread_join(struct thread* thread) {
	while (thread_state_get(thread) != THREAD_STATE_ZOMBIE)
		schedule();

	void* ret = (void*)thread->ctx.general.rax;
	atomic_sub_fetch(&thread->refcount, 1, ATOMIC_RELEASE);
	return ret;
}

_Noreturn void kthread_exit(void* ret) {
	struct thread* thread = current_cpu()->current_thread;
	thread->ctx.general.rax = (long)ret;
	thread_state_set(thread, THREAD_STATE_ZOMBIE);
	schedule();
	__builtin_unreachable();
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
