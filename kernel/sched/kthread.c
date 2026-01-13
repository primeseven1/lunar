#include <lunar/common.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/vmm.h>
#include <lunar/core/cpu.h>
#include <lunar/core/trace.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/asm/segment.h>
#include <lunar/asm/wrap.h>
#include <lunar/asm/flags.h>
#include <lunar/lib/format.h>
#include <lunar/lib/string.h>
#include <lunar/lib/hashtable.h>
#include "internal.h"

struct kthread {
	char name[24];
	int (*func)(void*);
	void* arg;
	bool scheduled;
};

static struct proc* kproc;
static struct hashtable* kthread_table;
static MUTEX_DEFINE(kthread_table_lock);

struct thread* kthread_create(int topology_flags, int (*func)(void*), void* arg, const char* fmt, ...) {
	struct thread* thread = thread_create(kproc, PAGE_SIZE * 4, topology_flags);
	if (!thread)
		return NULL;

	struct kthread kt = { .func = func, .arg = arg, .scheduled = false };
	va_list va;
	va_start(va, fmt);
	if (vsnprintf(kt.name, sizeof(kt.name), fmt, va) < 0)
		__builtin_strncpy(kt.name, "kthread", sizeof(kt.name));
	va_end(va);

	mutex_lock(&kthread_table_lock);

	int err = hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		thread_unref(thread); /* Remove the ref that thread_create() gives */
		bug(thread_destroy(thread) != 0);
		thread = NULL;
	}

	mutex_unlock(&kthread_table_lock);
	return thread;
}

int kthread_run(struct thread* thread, int prio) {
	mutex_lock(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		if (likely(err == -ENOENT))
			err = -EINVAL;
		goto out_unlock;
	}

	thread_prep_exec_kernel(thread, asm_kthread_start);
	err = sched_thread_attach(thread, prio);
	if (err)
		goto out_unlock;

	thread->ctx.general.rdi = (uintptr_t)kt.func;
	thread->ctx.general.rsi = (uintptr_t)kt.arg;
	err = sched_enqueue(thread);
	if (err) {
		sched_thread_detach(thread);
		goto out_unlock;
	}

	kt.scheduled = true;
	bug(hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt) != 0);
out_unlock:
	mutex_unlock(&kthread_table_lock);
	return err;
}

void kthread_destroy(struct thread* thread) {
	mutex_lock(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		if (err == -ENOENT)
			printk(PRINTK_ERR "sched: bug: kthread_destroy() called on either a detached thread or not a kthread");
		else
			printk(PRINTK_CRIT "sched: Unhandled error %i\n", err);
	} else {
		bug(kt.scheduled == true);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		thread_unref(thread);
		bug(thread_destroy(thread) != 0);
	}

	mutex_unlock(&kthread_table_lock);
}

void kthread_detach(struct thread* thread) {
	mutex_lock(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		printk(PRINTK_ERR "sched: bug: Failed to detach kthread with id %d: %d\n", thread->id, err);
	} else {
		bug(kt.scheduled == false);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		thread_unref(thread);
	}

	mutex_unlock(&kthread_table_lock);
}

_Noreturn void kthread_exit(int exit) {
	(void)exit;
	sched_thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

_Noreturn void __asmlinkage __kthread_start(int (*func)(void*), void* arg) {
	int ret = func(arg);
	kthread_exit(ret);
}

__diag_pop();

void kthread_init(void) {
	kproc = sched_get_from_proctbl(0);
	assert(IS_PTR_ERR(kproc) == false);

	kthread_table = hashtable_create(64, sizeof(struct kthread));
	if (unlikely(!kthread_table))
		panic("Failed to create kthread hashtable");
}
