#include <lunar/panic.h>
#include <lunar/hashtable.h>
#include <lunar/vmm.h>
#include <lunar/slab.h>
#include <lunar/sched.h>
#include "internal.h"

static struct hashtable* process_table = NULL;

void sched_proctbl_init(void) {
	process_table = hashtable_create(64, sizeof(struct proc*));
	if (!process_table)
		panic("Failed to create process table!\n");
}

int sched_add_to_proctbl(struct proc* proc) {
	return hashtable_insert(process_table, &proc->pid, sizeof(proc->pid), &proc);
}

int sched_get_from_proctbl(pid_t pid, struct proc** out) {
	return hashtable_search(process_table, &pid, sizeof(pid), out) != 0 ? -ESRCH : 0;
}

int sched_remove_proctbl(pid_t pid) {
	return hashtable_remove(process_table, &pid, sizeof(pid)) != 0 ? -ESRCH : 0;
}

void sched_thread_attach_to_proc(struct proc* proc, struct thread* thread) {
	unsigned long flags;
	spinlock_acquire_irq_save(&proc->lock, &flags);

	thread_ref(thread);
	list_add(&proc->thread_list, &thread->proc_link);
	atomic_fetch_add(&proc->thread_count, 1);
	bug(atomic_exchange(&thread->proc, proc) != NULL);

	spinlock_release_irq_restore(&proc->lock, &flags);
}

void sched_thread_detach_from_proc(struct thread* thread) {
	struct proc* proc = atomic_exchange(&thread->proc, NULL);
	bug(proc == NULL);

	unsigned long flags;
	spinlock_acquire_irq_save(&proc->lock, &flags);

	bug(!list_node_linked(&thread->proc_link));
	list_remove(&thread->proc_link);
	atomic_fetch_sub(&proc->thread_count, 1);
	thread_unref(thread);

	spinlock_release_irq_restore(&proc->lock, &flags);
}

static atomic(pid_t) current_pid = atomic_init(1);

pid_t sched_alloc_pid(void) {
	return atomic_fetch_add(&current_pid, 1);
}

void sched_free_pid(pid_t pid) {
	(void)pid;
}

static struct slab_cache* proc_cache;

struct proc* sched_proc_alloc(void) {
	struct proc* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return NULL;

	proc->pid = -1;
	proc->mm_struct = NULL;
	list_head_init(&proc->thread_list);
	atomic_store(&proc->thread_count, 0);
	spinlock_init(&proc->lock);
	proc->fs.cwd = NULL;
	proc->fs.root = NULL;
	mutex_init(&proc->fs.mtx);

	return proc;
}

void sched_proc_destroy(struct proc* proc) {
	bug(list_empty(&proc->thread_list) == false);
	slab_cache_free(proc_cache, proc);
}

void sched_proc_cache_init(void) {
	proc_cache = slab_cache_create(sizeof(struct proc), alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	if (!proc_cache)
		out_of_memory();
}
