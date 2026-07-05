#include <lunar/hashtable.h>
#include <lunar/proc.h>
#include <lunar/panic.h>
#include <lunar/init.h>
#include <lunar/slab.h>

static struct slab_cache* process_cache;
static struct hashtable* process_table;
static atomic(pid_t) pid_counter = atomic_init(1);
static struct proc kernel_proc = {
	.pid = 0,
	.cred = { .uid = 0, .euid = 0, .suid = 0, .gid = 0, .egid = 0, .sgid = 0 },
	.mm_struct = NULL,
	.threads = {
		.list = LIST_HEAD_INITIALIZER(kernel_proc.threads.list),
		.count = atomic_init(0),
		.lock = SPINLOCK_INITIALIZER
	},
	.fs = {
		.cwd = NULL, .root = NULL,
		.mtx = MUTEX_INITIALIZER(kernel_proc.fs.mtx)
	},
	.refcnt = atomic_init(1)
};

int proc_get(pid_t pid, struct proc** out) {
	if (pid != 0) {
		int err = hashtable_search(process_table, &pid, sizeof(pid), out);
		if (err)
			return -ESRCH;
	} else {
		*out = &kernel_proc;
	}
	PROC_HOLD(*out);
	return 0;
}

int proc_create(struct proc** out) {
	struct proc* proc = slab_cache_alloc(process_cache);
	if (!proc)
		return -ENOMEM;

	proc->mm_struct = mm_create();
	if (!proc->mm_struct) {
		slab_cache_free(process_cache, proc);
		return -ENOMEM;
	}
	proc->pid = atomic_fetch_add(&pid_counter, 1);
	proc->cred = *current_cred();
	list_head_init(&proc->threads.list);
	atomic_store(&proc->threads.count, 0);
	spinlock_init(&proc->threads.lock);
	proc->fs.cwd = NULL;
	proc->fs.root = NULL;
	mutex_init(&proc->fs.mtx);
	atomic_store(&proc->refcnt, 1);

	int err = hashtable_insert(process_table, &proc->pid, sizeof(proc->pid), &proc);
	if (err == 0) {
		*out = proc;
	} else {
		mm_destroy(proc->mm_struct);
		slab_cache_free(process_cache, proc);
	}
	return err;
}

void proc_inactive(struct proc* proc) {
	bug(proc == &kernel_proc);

	struct proc* _tmp;
	bug(hashtable_search(process_table, &proc->pid, sizeof(proc->pid), &_tmp) != 0 || _tmp != proc);

	bug(hashtable_remove(process_table, &proc->pid, sizeof(proc->pid)) != 0);
	mm_destroy(proc->mm_struct);
	slab_cache_free(process_cache, proc);
}

void proc_thread_attach(struct proc* proc, struct thread* thread) {
	unsigned long irq_flags;
	spinlock_acquire_irq_save(&proc->threads.lock, &irq_flags);

	THREAD_HOLD(thread);
	PROC_HOLD(proc);

	bug(atomic_exchange(&thread->proc, proc) != NULL);
	list_add(&proc->threads.list, &thread->proc_link);
	atomic_fetch_add(&proc->threads.count, 1);
	if (!thread->mm_struct)
		thread->mm_struct = proc->mm_struct;

	spinlock_release_irq_restore(&proc->threads.lock, &irq_flags);
}

void proc_thread_detach(struct thread* thread) {
	struct proc* proc = atomic_exchange(&thread->proc, NULL);

	unsigned long irq_flags;
	spinlock_acquire_irq_save(&proc->threads.lock, &irq_flags);

	bug(!list_node_linked(&thread->proc_link));
	list_remove(&thread->proc_link);
	atomic_fetch_sub(&proc->threads.count, 1);

	PROC_RELEASE(proc);
	THREAD_RELEASE(thread);

	spinlock_release_irq_restore(&proc->threads.lock, &irq_flags);
}

static void proc_init(void) {
	process_cache = slab_cache_create(sizeof(struct proc), alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	if (!process_cache)
		out_of_memory();
	process_table = hashtable_create(32, sizeof(struct proc*));
	if (!process_table)
		out_of_memory();
	kernel_proc.mm_struct = current_cpu()->mm_struct;
}

INIT_TASK_DECLARE(heap_init_task, vmm_init_task);
INIT_TASK_DEFINE(proc_init_task, INIT_TASK_SCOPE_BSP, proc_init, &heap_init_task, &vmm_init_task);
