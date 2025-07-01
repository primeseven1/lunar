#include <crescent/sched/sched.h>
#include <crescent/sched/kthread.h>
#include <crescent/asm/segment.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/cpu.h>
#include <crescent/core/trace.h>
#include <crescent/common.h>
#include <crescent/core/printk.h>
#include "sched.h"
#include "kthread.h"

struct thread* kthread_create(unsigned int flags, void* (*func)(void*), void* arg) {
	(void)flags;
	struct thread* thread = sched_thread_create();
	if (!thread)
		return NULL;

	void* stack = vmap(NULL, 0x4000, VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
	if (!stack) {
		sched_thread_destroy(thread);
		return NULL;
	}
	stack = (u8*)stack + 0x4000;
	thread->info.stack_top = stack;
	thread->info.stack_size = 0x4000;

	thread->ctx.general_regs.rip = asm_kthread_start;
	thread->ctx.general_regs.cs = SEGMENT_KERNEL_CODE;
	thread->ctx.general_regs.rflags = 0x202;
	thread->ctx.general_regs.ss = SEGMENT_KERNEL_DATA;
	thread->ctx.general_regs.rsp = stack;
	thread->ctx.general_regs.rdi = (long)func;
	thread->ctx.general_regs.rsi = (long)arg;

	sched_schedule(thread, NULL);
	return thread;
}

void* kthread_join(struct thread* thread) {
	while (__atomic_load_n(&thread->state, __ATOMIC_SEQ_CST) != THREAD_STATE_ZOMBIE)
		__asm__ volatile("pause");

	void* ret = (void*)thread->ctx.general_regs.rax;
	__atomic_sub_fetch(&thread->refcount, 1, __ATOMIC_SEQ_CST);
	return ret;
}

_Noreturn void kthread_exit(void* ret) {
	struct thread* thread = current_cpu()->current_thread;

	thread->ctx.general_regs.rax = (long)ret;
	__atomic_store_n(&thread->state, THREAD_STATE_ZOMBIE, __ATOMIC_SEQ_CST);

	/* TODO: add sched_yeild when implemented */
	while (1)
		__asm__ volatile("hlt");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage void __kthread_start(void* (*func)(void* arg), void* arg) {
	func(arg);
	printk(PRINTK_EMERG "sched: Function at %p did not call kthread_exit!\n", func);
	/* asm_kthread_startup causes a trap already, so just return here */
}

__diag_pop();
