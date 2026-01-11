#include <lunar/common.h>
#include <lunar/asm/segment.h>
#include <lunar/sched/scheduler.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/core/cpu.h>
#include <lunar/core/trace.h>
#include <lunar/core/mutex.h>
#include <lunar/lib/string.h>
#include <lunar/mm/slab.h>
#include <lunar/mm/heap.h>
#include "internal.h"

#define THREAD_STACK_GUARD_SIZE PAGE_SIZE

static struct slab_cache* proc_cache;
static struct slab_cache* thread_cache;

static int stacks_create(struct thread* thread, size_t rstack_size) {
	if (thread->proc->pid == KERNEL_PID) {
		thread->utk_stack = NULL;
		thread->ustack = NULL;
		thread->kstack = vmap_stack(rstack_size, true);
		return IS_PTR_ERR(thread->kstack) ? PTR_ERR(thread->kstack) : 0;
	}

	thread->kstack = NULL;
	thread->ustack = uvmap_stack(rstack_size, true, thread->proc->mm_struct);
	if (IS_PTR_ERR(thread->ustack))
		return PTR_ERR((__force void*)thread->ustack);
	const size_t utk_size = PAGE_SIZE * 4;
	thread->utk_stack = vmap_stack(utk_size, true);
	thread->utk_stack_size = utk_size;
	if (IS_PTR_ERR(thread->utk_stack)) {
		bug(uvunmap_stack(thread->ustack, rstack_size, true, thread->proc->mm_struct) != 0);
		return PTR_ERR(thread->utk_stack);
	}

	return 0;
}

static void stacks_destroy(struct thread* thread) {
	if (thread->proc->pid == KERNEL_PID) {
		bug(vunmap_stack(thread->kstack, thread->stack_size, true) != 0);
		return;
	}

	bug(uvunmap_stack(thread->ustack, thread->stack_size, true, thread->proc->mm_struct) != 0);
	bug(vunmap_stack(thread->utk_stack, thread->utk_stack_size, true) != 0);
}

struct thread* thread_create(struct proc* proc, size_t stack_size, int topology_flags) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;
	thread->proc = proc;

	/* Start out with all allocations before setting any more values in the thread struct */
	int topology_err = -ERRNO_MAX;
	void* ext_ctx = NULL;
	tid_t tid = tid_alloc(proc);
	if (thread->id == -1)
		goto err;
	/* Handles thread->topology */
	topology_err = topology_thread_init(thread, topology_flags);
	if (topology_err)
		goto err;
	ext_ctx = ext_ctx_alloc();
	if (!ext_ctx)
		goto err;
	/* Sets thread->kstack, thread->ustack, and thread->utk_stack */
	if (stacks_create(thread, stack_size) != 0)
		goto err;

	thread->id = tid;
	thread->attached = false;
	thread->in_usercopy = false;
	thread->prio = 0;
	atomic_store_explicit(&thread->state, THREAD_NEW, ATOMIC_RELAXED);
	thread->wakeup_time = 0;
	atomic_store_explicit(&thread->wakeup_err, 0, ATOMIC_RELAXED);
	atomic_store_explicit(&thread->sleep_interruptable, false, ATOMIC_RELAXED);
	atomic_store_explicit(&thread->should_exit, false, ATOMIC_RELAXED);
	thread->preempt_count = 0;
	__builtin_memset(&thread->ctx.general, 0, sizeof(thread->ctx.general));
	thread->ctx.fsbase = NULL;
	thread->ctx.gsbase = NULL;
	thread->ctx.extended = ext_ctx;
	list_node_init(&thread->proc_link);
	list_node_init(&thread->sleep_link);
	list_node_init(&thread->zombie_link);
	list_node_init(&thread->block_link);
	thread->policy_priv = NULL;
	atomic_store_explicit(&thread->refcnt, 1, ATOMIC_RELAXED);

	return thread;
err:
	if (tid != -1)
		tid_free(proc, tid);
	if (topology_err == 0)
		topology_thread_destroy(thread);
	if (ext_ctx)
		ext_ctx_free(ext_ctx);
	slab_cache_free(thread_cache, thread);
	return NULL;
}

void thread_prep_exec_kernel(struct thread* thread, void* exec) {
	thread->ctx.general.rip = (uintptr_t)exec;
	thread->ctx.general.rflags = THREAD_RFLAGS_DEFAULT;
	thread->ctx.general.rsp = (uintptr_t)thread->kstack;
	thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
	thread->ctx.general.ds = SEGMENT_KERNEL_DATA;
	thread->ctx.general.es = SEGMENT_KERNEL_DATA;
	thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
	topology_pick_cpu(thread);
}

void thread_prep_exec_user(struct thread* thread, void __user* exec) {
	thread->ctx.general.rip = (uintptr_t)exec;
	thread->ctx.general.rflags = THREAD_RFLAGS_DEFAULT;
	thread->ctx.general.rsp = (uintptr_t)thread->ustack;
	thread->ctx.general.cs = SEGMENT_USER_CODE;
	thread->ctx.general.ds = SEGMENT_USER_DATA;
	thread->ctx.general.es = SEGMENT_USER_DATA;
	thread->ctx.general.ss = SEGMENT_USER_DATA;
	topology_pick_cpu(thread);
}

int thread_destroy(struct thread* thread) {
	if (atomic_load(&thread->refcnt) != 0)
		return -EBUSY;

	stacks_destroy(thread);
	topology_thread_destroy(thread);
	tid_free(thread->proc, thread->id);
	ext_ctx_free(thread->ctx.extended);

	slab_cache_free(thread_cache, thread);
	return 0;
}

int sched_proc_create(const struct cred* cred, struct mm* mm_struct, struct proc** out) {
	struct proc* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return -ENOMEM;

	proc->pid = pid_alloc();
	if (unlikely(proc->pid == -1)) {
		slab_cache_free(proc_cache, proc);
		return -EAGAIN;
	}

	proc->cred = *cred;
	proc->mm_struct = mm_struct;
	spinlock_init(&proc->tid_lock);
	int err = tid_create_bitmap(proc);
	if (err) {
		pid_free(proc->pid);
		slab_cache_free(proc_cache, proc);
		return -ENOMEM;
	}

	list_head_init(&proc->threads);
	atomic_store(&proc->thread_count, 0);
	spinlock_init(&proc->thread_lock);

	*out = proc;
	return 0;
}

int sched_proc_destroy(struct proc* proc) {
	if (atomic_load(&proc->thread_count))
		return -EBUSY;

	tid_free_bitmap(proc);
	pid_free(proc->pid);

	slab_cache_free(proc_cache, proc);
	return 0;
}

int thread_add_to_proc(struct thread* thread) {
	struct proc* proc = thread->proc;
	irqflags_t irq;
	spinlock_lock_irq_save(&proc->thread_lock, &irq);

	int err = 0;
	if (list_node_linked(&thread->proc_link)) {
		err = -EALREADY;
		goto err;
	}

	atomic_add_fetch(&proc->thread_count, 1);
	list_add(&proc->threads, &thread->proc_link);
	thread_ref(thread);
err:
	spinlock_unlock_irq_restore(&proc->thread_lock, &irq);
	return err;
}

int thread_remove_from_proc(struct thread* thread) {
	struct proc* proc = thread->proc;
	irqflags_t irq;
	spinlock_lock_irq_save(&proc->thread_lock, &irq);

	int err = 0;
	if (!list_node_linked(&thread->proc_link)) {
		err = -ENOENT;
		goto err;
	}

	list_remove(&thread->proc_link);
	atomic_sub_fetch(&proc->thread_count, 1);
	thread_unref(thread);
err:
	spinlock_unlock_irq_restore(&proc->thread_lock, &irq);
	return err;
}

void procthrd_init(void) {
	proc_cache = slab_cache_create(sizeof(struct proc), _Alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	thread_cache = slab_cache_create(sizeof(struct thread), _Alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	if (unlikely(!thread_cache || !proc_cache))
		panic("procthrd_init() failed!");
}
