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

thread_t* kthread_create(unsigned int flags, void* (*func)(void*), void* arg) {
	thread_t* thread = sched_thread_alloc();
	if (!thread)
		return NULL;

	void* stack = vmap(NULL, 0x4000, VMAP_ALLOC, MMU_READ | MMU_WRITE, NULL);
	if (!stack) {
		sched_thread_free(thread);
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

	sched_schedule_new_thread(thread, NULL, flags);
	return thread;
}

void* kthread_join(thread_t* thread) {
	while (atomic_load(&thread->state, ATOMIC_SEQ_CST) != THREAD_STATE_ZOMBIE)
		__asm__ volatile("pause");

	void* ret = (void*)thread->ctx.general_regs.rax;
	atomic_sub_fetch(&thread->refcount, 1, ATOMIC_SEQ_CST);
	return ret;
}

_Noreturn void kthread_exit(void* ret) {
	thread_t* thread = current_cpu()->current_thread;

	thread->ctx.general_regs.rax = (long)ret;
	atomic_store(&thread->state, THREAD_STATE_ZOMBIE, ATOMIC_SEQ_CST);

	/* TODO: add sched_yeild when implemented */
	while (1)
		__asm__ volatile("hlt");
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage void __kthread_start(void* (*func)(void*), void* arg) {
	func(arg);
	printk(PRINTK_EMERG "sched: Function at %p did not call kthread_exit!\n", func);
	/* asm_kthread_startup causes a trap already, so just return here */
}

__diag_pop();
