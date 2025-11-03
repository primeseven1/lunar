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
#define RFLAGS_DEFAULT 0x202

static struct slab_cache* proc_cache;
static struct slab_cache* thread_cache;

static u8* pid_map = NULL;
static SPINLOCK_DEFINE(pid_lock);
static const pid_t pid_max = 0x10000;
static const tid_t tid_max = 0x10000;

static long long alloc_id(u8* map, long long max) {
	for (long long id = 0; id < max; id++) {
		size_t byte = id >> 3;
		unsigned int bit = id & 7;
		if ((map[byte] & (1 << bit)) == 0) {
			map[byte] |= (1 << bit);
			return id;
		}
	}

	return max;
}

static inline void free_id(u8* map, long long id, long long max) {
	bug(id > max);
	size_t byte = id >> 3;
	unsigned int bit = id & 7;
	map[byte] &= ~(1 << bit);
}

struct thread* thread_create(struct proc* proc, void* exec, size_t stack_size) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	thread->id = alloc_id(proc->tid_map, tid_max);
	if (unlikely(thread->id == tid_max))
		goto err_id;

	thread->target_cpu = NULL;
	thread->cpu_mask = ULONG_MAX;
	thread->attached = false;
	thread->proc = proc;
	thread->ring = proc->pid == KERNEL_PID ? THREAD_RING_KERNEL : THREAD_RING_USER;
	thread->prio = 0;
	thread->wakeup_time = 0;
	atomic_store_explicit(&thread->state, THREAD_NEW, ATOMIC_RELAXED);
	thread->wakeup_time = 0;
	atomic_store_explicit(&thread->wakeup_err, 0, ATOMIC_RELAXED);
	atomic_store_explicit(&thread->sleep_interruptable, 0, ATOMIC_RELAXED);
	thread->should_exit = false;
	thread->preempt_count = 0;

	stack_size = ROUND_UP(stack_size, PAGE_SIZE);
	const size_t stack_total = stack_size + THREAD_STACK_GUARD_SIZE;
	thread->stack = vmap(NULL, stack_total, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!thread->stack)
		goto err_stack;
	bug(vprotect(thread->stack, THREAD_STACK_GUARD_SIZE, MMU_NONE, 0) != 0); /* guard page */
	thread->stack_size = stack_size;

	memset(&thread->ctx.general, 0, sizeof(thread->ctx.general));
	thread->ctx.thread_local = NULL;
	thread->ctx.extended = ext_ctx_alloc();
	if (!thread->ctx.extended)
		goto err_ctx;
	thread->ctx.general.rflags = RFLAGS_DEFAULT;
	thread->ctx.general.rsp = (u8*)thread->stack + stack_total;
	thread->ctx.general.rip = exec;
	if (thread->ring == THREAD_RING_KERNEL) {
		thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
		thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
	} else {
		thread->ctx.general.cs = SEGMENT_USER_CODE;
		thread->ctx.general.ss = SEGMENT_USER_DATA;
	}
	
	list_node_init(&thread->proc_link);
	list_node_init(&thread->sleep_link);
	list_node_init(&thread->zombie_link);
	list_node_init(&thread->block_link);

	thread->policy_priv = NULL;
	atomic_store_explicit(&thread->refcount, 0, ATOMIC_RELAXED);
	return thread;
err_ctx:
	assert(vunmap(thread->stack, stack_total, 0) == 0);
err_stack:
	free_id(proc->tid_map, thread->id, tid_max);
err_id:
	return NULL;
}

int thread_destroy(struct thread* thread) {
	if (atomic_load(&thread->refcount) != 0)
		return -EBUSY;

	const size_t stack_total = thread->stack_size + THREAD_STACK_GUARD_SIZE;
	assert(vunmap(thread->stack, stack_total, 0) == 0);
	free_id(thread->proc->tid_map, thread->id, tid_max);
	ext_ctx_free(thread->ctx.extended);

	slab_cache_free(thread_cache, thread);
	return 0;
}

struct proc* proc_create(const struct cred* cred) {
	struct proc* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return NULL;

	irqflags_t irq;
	spinlock_lock_irq_save(&pid_lock, &irq);
	proc->pid = alloc_id(pid_map, pid_max);
	spinlock_unlock_irq_restore(&pid_lock, &irq);

	if (unlikely(proc->pid == pid_max)) {
		slab_cache_free(proc_cache, proc);
		return NULL;
	}

	proc->mm_struct = NULL;

	proc->tid_map = kzalloc((tid_max + 7) >> 3, MM_ZONE_NORMAL);
	if (!proc->tid_map) {
		free_id(pid_map, proc->pid, pid_max);
		slab_cache_free(proc_cache, proc);
		return NULL;
	}

	proc->cred = *cred;

	list_head_init(&proc->threads);
	atomic_store(&proc->thread_count, 0);
	spinlock_init(&proc->thread_lock);

	return proc;
}

int proc_destroy(struct proc* proc) {
	if (atomic_load(&proc->thread_count))
		return -EBUSY;

	irqflags_t irq;
	spinlock_lock_irq_save(&pid_lock, &irq);
	free_id(pid_map, proc->pid, pid_max);
	spinlock_unlock_irq_restore(&pid_lock, &irq);
	kfree(proc->tid_map);

	slab_cache_free(proc_cache, proc);
	return 0;
}

int thread_attach_to_proc(struct thread* thread) {
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

err:
	spinlock_unlock_irq_restore(&proc->thread_lock, &irq);
	return err;
}

int thread_detach_from_proc(struct thread* thread) {
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
err:
	spinlock_unlock_irq_restore(&proc->thread_lock, &irq);
	return err;
}

void procthrd_init(void) {
	proc_cache = slab_cache_create(sizeof(struct proc), _Alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	assert(proc_cache != NULL);
	const size_t pid_map_size = (pid_max + 7) >> 3;
	pid_map = vmap(NULL, pid_map_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	assert(pid_map != NULL);

	thread_cache = slab_cache_create(sizeof(struct thread), _Alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	assert(thread_cache != NULL);
}
