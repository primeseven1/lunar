#include <crescent/common.h>
#include <crescent/sched/scheduler.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/trace.h>
#include <crescent/lib/string.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/heap.h>
#include "sched.h"

static struct slab_cache* proc_cache;
static struct slab_cache* thread_cache;

static u8* pid_map = NULL;
static spinlock_t pid_map_lock = SPINLOCK_INITIALIZER;
static const pid_t max_pid_count = 0x100000;

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

static inline void free_id(u8* map, long long id) {
	size_t byte = id >> 3;
	unsigned int bit = id & 7;
	map[byte] &= ~(1 << bit);
}

static inline bool check_id_max(long long id, long long max) {
	if (unlikely(id > max)) {
		printk(PRINTK_ERR "sched: id %llu > %llu\n", id, max);
		return false;
	}
	return true;
}

static pid_t alloc_pid(void) {
	unsigned long flags;
	spinlock_lock_irq_save(&pid_map_lock, &flags);
	pid_t ret = alloc_id(pid_map, max_pid_count);
	spinlock_unlock_irq_restore(&pid_map_lock, &flags);
	return ret;
}

static void free_pid(pid_t pid) {
	if (!check_id_max(pid, max_pid_count))
		return;

	unsigned long flags;
	spinlock_lock_irq_save(&pid_map_lock, &flags);
	free_id(pid_map, pid);
	spinlock_unlock_irq_restore(&pid_map_lock, &flags);
}

struct proc* proc_alloc(void) {
	struct proc* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return NULL;

	proc->pid = alloc_pid();
	if (proc->pid == max_pid_count) {
		slab_cache_free(proc_cache, proc);
		return NULL;
	}

	atomic_store(&proc->thread_count, 0, ATOMIC_RELAXED);
	proc->mm_struct = NULL;
	proc->parent = NULL;
	proc->sibling = NULL;
	proc->child = NULL;

	return proc;
}

void proc_free(struct proc* proc) {
	slab_cache_free(proc_cache, proc);
	free_pid(proc->pid);
}

struct thread* thread_alloc(void) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	memset(thread, 0, sizeof(*thread));
	return thread;
}

void thread_free(struct thread* thread) {
	slab_cache_free(thread_cache, thread);
}

void proc_thread_alloc_init(void) {
	proc_cache = slab_cache_create(sizeof(struct proc), _Alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	assert(proc_cache != NULL);
	const size_t pid_map_size = (max_pid_count + 7) >> 3;
	pid_map = vmap(NULL, pid_map_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	assert(pid_map != NULL);

	thread_cache = slab_cache_create(sizeof(struct thread), _Alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	assert(thread_cache != NULL);
}
