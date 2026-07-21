#include <lunar/kthread.h>
#include <lunar/proc.h>
#include <lunar/format.h>
#include <lunar/mutex.h>
#include <lunar/init.h>
#include <lunar/hashtable.h>
#include <lunar/trace.h>
#include <lunar/vmm.h>
#include <lunar/string.h>
#include <lunar/printk.h>

#include <arch/kthread.h>

typedef int (*kthread_entry_t)(void*); /* Makes this play nice with the atomic() macro */

union kthread_asm_arg {
	struct {
		kthread_entry_t threadfn;
		void* arg;
	} nonatomic;
	struct {
		atomic(kthread_entry_t) threadfn;
		atomic(void*) arg;
	} atomic;
};
static_assert(sizeof(union kthread_asm_arg) == 16);

struct kthread {
	char name[32];
	union kthread_asm_arg arg;
	void* stack_bottom, *stack_top;
	bool scheduled;
};

static struct proc* kernel_proc;

static struct hashtable* kthread_table;
static MUTEX_DEFINE(kthread_table_lock);

struct thread* kthread_create(int flags, int (*threadfn)(void*), void* arg, const char* fmt, ...) {
	struct thread* thread = alloc_thread(flags);
	if (!thread)
		return NULL;

	struct kthread kt;
	va_list va;
	va_start(va, fmt);
	if (vsnprintf(kt.name, sizeof(kt.name), fmt, va) < 0)
		strlcpy(kt.name, "kthread", sizeof(kt.name));
	va_end(va);
	kt.arg = (union kthread_asm_arg){ .nonatomic = { .threadfn = threadfn, .arg = arg } };
	int err = alloc_thread_stack(thread, sizeof(kt.arg), &kt.stack_bottom, &kt.stack_top);
	if (err) {
		THREAD_RELEASE(thread);
		free_thread(thread);
		return NULL;
	}
	kt.scheduled = false;

	mutex_acquire(&kthread_table_lock);

	err = hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		vm_unmap_force(kt.stack_bottom, (THREAD_STACK_SIZE >> PAGE_SHIFT) + 1, 0);
		THREAD_RELEASE(thread); /* Remove the ref that alloc_thread() gives */
		free_thread(thread);
		thread = NULL;
	}

	mutex_release(&kthread_table_lock);
	return thread;
}

int kthread_run(struct thread* thread, int prio) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		if (likely(err == -ENOENT))
			err = -EINVAL;
		goto out_unlock;
	}
	if (kt.scheduled)
		goto out_unlock;

	err = sched_thread_attach(thread, kernel_proc, prio);
	if (err)
		goto out_unlock;

	/* Prepare the stack */
	union kthread_asm_arg* thread_args = (union kthread_asm_arg*)kt.stack_top - 1;
	atomic_store(&thread_args->atomic.threadfn, kt.arg.nonatomic.threadfn);
	atomic_store(&thread_args->atomic.arg, kt.arg.nonatomic.arg);

	const struct thread_entry_point entry_point = { .kernel_entry = arch_asm_kthread_start, .user_entry = NULL };
	arch_thread_prepare_execution(thread, &entry_point);

	err = sched_enqueue(thread);
	if (err) {
		sched_thread_detach(thread);
		goto out_unlock;
	}

	kt.scheduled = true;
	bug(hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt) != 0);
out_unlock:
	mutex_release(&kthread_table_lock);
	return err;
}

void kthread_destroy(struct thread* thread) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		printk(PRINTK_ERR "kthread: %s() invalid pointer (err %d)\n", __func__, err);
	} else {
		bug(kt.scheduled == true);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		THREAD_RELEASE(thread);
		free_thread(thread);
	}

	mutex_release(&kthread_table_lock);
}

void kthread_detach(struct thread* thread) {
	mutex_acquire(&kthread_table_lock);

	struct kthread kt;
	int err = hashtable_search(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		dump_stack();
		printk(PRINTK_ERR "kthread: %s() invalid pointer (err %d)\n", __func__, err);
	} else {
		bug(kt.scheduled == false);
		free_thread_stack(kt.stack_bottom);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		THREAD_RELEASE(thread);
	}

	mutex_release(&kthread_table_lock);
}

_Noreturn void kthread_exit(int exit) {
	(void)exit;
	sched_thread_exit();
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

_Noreturn void __asmlinkage kthread_start(int (*func)(void*), void* arg) {
	int ret = func(arg);
	kthread_exit(ret);
}

__diag_pop();

static void kthread_init(void) {
	bug(proc_get(0, &kernel_proc) != 0);
	kthread_table = hashtable_create(64, sizeof(struct kthread));
	if (unlikely(!kthread_table))
		panic("Failed to create kthread hashtable");
}

INIT_TASK_DECLARE(sched_init_task, heap_init_task);
INIT_TASK_DEFINE(kthread_init_task, INIT_TASK_SCOPE_BSP, kthread_init, &sched_init_task, &heap_init_task);
