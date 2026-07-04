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

struct kthread {
	char name[32];
	int (*func)(void*);
	void* arg;
	struct thread_stack stack;
	bool scheduled;
};

static struct proc* kernel_proc;
static struct hashtable* kthread_table;
static MUTEX_DEFINE(kthread_table_lock);

typedef atomic(void*) atomic_void_ptr_t; /* Don't really like doing this, since this allows more ways to use it wrong */

struct thread* kthread_create(int flags, int (*func)(void*), void* arg, const char* fmt, ...) {
	(void)flags;

	struct thread* thread = sched_thread_alloc(flags);
	if (!thread)
		return NULL;
	const size_t stack_size = PAGE_SIZE * 4;
	atomic_void_ptr_t* stack = vmap(NULL, stack_size + PAGE_SIZE, PGPROT_READ | PGPROT_WRITE, VMM_ALLOC | VMM_STACK, NULL);
	if (IS_PTR_ERR(stack)) {
		THREAD_RELEASE(thread);
		sched_thread_destroy(thread);
		return NULL;
	}
	void* const stack_base = stack;

	bug(vprotect(stack, PAGE_SIZE, PGPROT_NONE, 0, NULL) != 0);
	stack = (atomic_void_ptr_t*)((u8*)stack + stack_size + PAGE_SIZE);
	stack -= 2;

	atomic_void_ptr_t* _fn = stack;
	atomic_void_ptr_t* _arg = stack + 1;
	const size_t ptr_off = sizeof(*_fn) + sizeof(*_arg);

	atomic_store_explicit(_fn, func, ATOMIC_SEQ_CST);
	atomic_store_explicit(_arg, arg, ATOMIC_SEQ_CST);
	stack += 2;

	struct kthread kt = {
		.func = func, .arg = arg,
		.stack = {
			.kernel_stack_top = stack, .kernel_ptr_off = ptr_off, .kernel_size = stack_size, .kernel_guard_size = PAGE_SIZE,
			.user_stack_top = NULL, .user_ptr_off = 0, .user_size = 0, .user_guard_size = 0
		},
		.scheduled = false
	};
	va_list va;
	va_start(va, fmt);
	if (vsnprintf(kt.name, sizeof(kt.name), fmt, va) < 0)
		strlcpy(kt.name, "kthread", sizeof(kt.name));
	va_end(va);

	mutex_acquire(&kthread_table_lock);

	int err = hashtable_insert(kthread_table, &thread, sizeof(struct thread*), &kt);
	if (err) {
		bug(vunmap(stack_base, stack_size + PAGE_SIZE, 0, NULL) != 0);
		THREAD_RELEASE(thread); /* Remove the ref that sched_thread_alloc() gives */
		sched_thread_destroy(thread);
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

	const struct thread_entry_point entry_point = { .kernel_entry = arch_asm_kthread_start, .user_entry = NULL };
	thread->stack = kt.stack;
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
		size_t unmap_size = kt.stack.kernel_size + kt.stack.kernel_guard_size;
		void* const stack_base = (u8*)kt.stack.kernel_stack_top - unmap_size;
		bug(vunmap(stack_base, unmap_size, 0, NULL) != 0);
		bug(hashtable_remove(kthread_table, &thread, sizeof(struct thread*)) != 0);
		THREAD_RELEASE(thread);
		sched_thread_destroy(thread);
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
