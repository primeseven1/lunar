#include <crescent/common.h>
#include <crescent/sched/scheduler.h>
#include <crescent/sched/kthread.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/cpu.h>
#include <crescent/core/trace.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/asm/segment.h>
#include <crescent/asm/wrap.h>
#include <crescent/asm/flags.h>
#include <crescent/lib/format.h>
#include <crescent/lib/string.h>
#include <crescent/lib/hashtable.h>
#include "internal.h"

struct kthread {
	char name[40];
	struct thread* thread;
};

static struct proc* kproc;
static struct hashtable* kthread_table;

tid_t kthread_create(int sched_flags, int (*func)(void*), void* arg, const char* fmt, ...) {
	struct thread* thread = thread_create(kproc, KSTACK_SIZE);
	if (!thread)
		return -ENOMEM;

	thread_set_ring(thread, THREAD_RING_KERNEL);
	thread_set_exec(thread, asm_kthread_start);
	thread->target_cpu = sched_decide_cpu(sched_flags);

	int err = sched_thread_attach(&thread->target_cpu->runqueue, thread, SCHED_PRIO_DEFAULT);
	if (err)
		goto err_attach;

	atomic_add_fetch(&thread->refcount, 1);
	thread->ctx.general.rdi = (uintptr_t)func;
	thread->ctx.general.rsi = (uintptr_t)arg;

	/* Now set up the kthread structure */
	struct kthread kthread_struct;
	kthread_struct.thread = thread;
	va_list va;
	va_start(va, fmt);
	int count = vsnprintf(kthread_struct.name, sizeof(kthread_struct.name), fmt, va);
	if (unlikely(count < 0)) {
		printk(PRINTK_WARN "sched: vsnprintf format failed on kthread name!\n");
		strlcpy(kthread_struct.name, "kthread", sizeof(kthread_struct.name));
	} else if ((size_t)count >= sizeof(kthread_struct.name)) {
		printk(PRINTK_WARN "sched: kthread name too long!\n");
	}
	va_end(va);

	/* Make sure there is no collision (very unlikely) */
	bug(hashtable_search(kthread_table, &thread->id, sizeof(thread->id), &kthread_struct) == 0);
	err = hashtable_insert(kthread_table, &thread->id, sizeof(thread->id), &kthread_struct);
	if (err)
		goto err_hashtable;

	err = sched_enqueue(&thread->target_cpu->runqueue, thread);
	if (unlikely(err))
		goto err;

	return thread->id;
err:
	hashtable_remove(kthread_table, &thread->id, sizeof(thread->id));
err_hashtable:
	sched_thread_detach(&thread->target_cpu->runqueue, thread);
err_attach:
	thread_destroy(thread);
	return err;
}

int kthread_detach(tid_t id) {
	struct kthread kthread_struct;
	int err = hashtable_search(kthread_table, &id, sizeof(id), &kthread_struct);
	if (err == -ENOENT)
		return -ESRCH;
	else if (unlikely(err))
		return err;

	bug(hashtable_remove(kthread_table, &id, sizeof(id)) != 0);
	struct thread* thread = kthread_struct.thread;
	bug(atomic_fetch_sub(&thread->refcount, 1) == 0);

	return 0;
}

int kthread_wait_for_completion(tid_t id) {
	struct kthread kthread_struct;
	int err = hashtable_search(kthread_table, &id, sizeof(id), &kthread_struct);
	if (err == -ENOENT)
		return -ESRCH;
	else if (unlikely(err))
		return err;

	struct thread* thread = kthread_struct.thread;
	while (atomic_load(&thread->state) != THREAD_ZOMBIE)
		schedule();

	return 0;
}

_Noreturn void kthread_exit(int exit) {
	(void)exit;
	sched_thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

__asmlinkage _Noreturn void __kthread_start(int (*func)(void*), void* arg) {
	int ret = func(arg);
	printk(PRINTK_WARN "kthread failed to call kthread_exit!\n");
	dump_stack();
	kthread_exit(ret);
}

__diag_pop();

void kthread_init(struct proc* kernel_proc) {
	kproc = kernel_proc;
	kthread_table = hashtable_create(50, sizeof(struct kthread));
	if (unlikely(!kthread_table))
		panic("Failed to create kthread hashtable");
}
