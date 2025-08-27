#include <crescent/common.h>
#include <crescent/asm/segment.h>
#include <crescent/sched/scheduler.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/trace.h>
#include <crescent/core/mutex.h>
#include <crescent/lib/string.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/heap.h>
#include "crescent/core/spinlock.h"
#include "crescent/types.h"
#include "internal.h"

#define THREAD_STACK_GUARD_SIZE PAGE_SIZE
#define RFLAGS_DEFAULT 0x202

static struct slab_cache* proc_cache;
static struct slab_cache* thread_cache;

static u8* pid_map = NULL;
static spinlock_t pid_lock = SPINLOCK_INITIALIZER;
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
	assert(id <= max);
	size_t byte = id >> 3;
	unsigned int bit = id & 7;
	map[byte] &= ~(1 << bit);
}

struct thread* thread_create(struct proc* proc, size_t stack_size) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	thread->id = alloc_id(proc->tid_map, tid_max);
	if (unlikely(thread->id == tid_max))
		goto err_id;

	thread->target_cpu = NULL; /* Let the scheduler decide what CPU to schedule on */
	thread->proc = proc;
	thread->cpu_mask = ULONG_MAX;
	atomic_store(&thread->state, THREAD_NEW, ATOMIC_RELAXED);

	if (stack_size & (PAGE_SIZE - 1)) {
		printk(PRINTK_WARN "sched: stack size not a multiple of page size!\n");
		stack_size = ROUND_UP(stack_size, PAGE_SIZE);
	}
	const size_t stack_total = stack_size + THREAD_STACK_GUARD_SIZE;
	thread->stack = vmap(NULL, stack_total, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (!thread->stack)
		goto err_stack;
	assert(vprotect(thread->stack, THREAD_STACK_GUARD_SIZE, MMU_NONE, 0) == 0); /* guard page */
	thread->stack_size = stack_size;

	memset(&thread->ctx.general, 0, sizeof(thread->ctx.general));
	thread->ctx.extended = ext_ctx_alloc();
	if (!thread->ctx.extended)
		goto err_ctx;

	thread->preempt_count = 0;
	
	list_node_init(&thread->proc_link);
	list_node_init(&thread->sleep_link);
	list_node_init(&thread->zombie_link);
	list_node_init(&thread->block_link);

	thread->ctx.general.rflags = RFLAGS_DEFAULT;
	thread->ctx.general.rsp = (u8*)thread->stack + stack_total;

	atomic_store(&thread->refcount, 0, ATOMIC_RELEASE);
	return thread;
err_ctx:
	assert(vunmap(thread->stack, stack_total, 0) == 0);
err_stack:
	free_id(proc->tid_map, thread->id, tid_max);
err_id:
	return NULL;
}

void thread_add_to_proc(struct proc* proc, struct thread* thread) {
	unsigned long irq;
	spinlock_lock_irq_save(&proc->thread_lock, &irq);

	list_add(&proc->threads, &thread->proc_link);
	atomic_add_fetch(&proc->thread_count, 1, ATOMIC_ACQUIRE);

	spinlock_unlock_irq_restore(&proc->thread_lock, &irq);
}

int thread_destroy(struct thread* thread) {
	if (atomic_load(&thread->refcount, ATOMIC_ACQUIRE) != 0)
		return -EBUSY;

	const size_t stack_total = thread->stack_size + THREAD_STACK_GUARD_SIZE;
	assert(vunmap(thread->stack, stack_total, 0) == 0);
	free_id(thread->proc->tid_map, thread->id, tid_max);
	ext_ctx_free(thread->ctx.extended);
	slab_cache_free(thread_cache, thread);

	return 0;
}

int thread_set_ring(struct thread* thread, int ring) {
	switch (ring) {
	case THREAD_RING_KERNEL:
		thread->ctx.general.cs = SEGMENT_KERNEL_CODE;
		thread->ctx.general.ss = SEGMENT_KERNEL_DATA;
		break;
	case THREAD_RING_USER:
		thread->ctx.general.cs = SEGMENT_USER_CODE;
		thread->ctx.general.ss = SEGMENT_USER_DATA;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct proc* proc_create(void) {
	struct proc* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return NULL;

	unsigned long irq;
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

	list_head_init(&proc->threads);
	atomic_store(&proc->thread_count, 0, ATOMIC_RELAXED);
	spinlock_init(&proc->thread_lock);

	return proc;
}

int proc_destroy(struct proc* proc) {
	if (atomic_load(&proc->thread_count, ATOMIC_ACQUIRE))
		return -EBUSY;

	unsigned long irq;
	spinlock_lock_irq_save(&pid_lock, &irq);
	free_id(pid_map, proc->pid, pid_max);
	spinlock_unlock_irq_restore(&pid_lock, &irq);
	kfree(proc->tid_map);

	slab_cache_free(proc_cache, proc);
	return 0;
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
