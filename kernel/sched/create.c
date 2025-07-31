#include <crescent/common.h>
#include <crescent/sched/sched.h>
#include <crescent/core/panic.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/lib/string.h>
#include <crescent/mm/slab.h>
#include <crescent/mm/heap.h>
#include "sched.h"

static struct slab_cache* proc_cache;
static struct slab_cache* thread_cache;

static u8* pid_map = NULL;
static spinlock_t pid_map_lock = SPINLOCK_INITIALIZER;
static const pid_t max_pid_count = 0x100000;

static pid_t alloc_pid(void) {
	unsigned long flags;
	spinlock_lock_irq_save(&pid_map_lock, &flags);

	pid_t ret = max_pid_count;
	for (pid_t pid = 0; pid < max_pid_count; pid++) {
		size_t byte_index = pid / 8;
		unsigned int bit_index = pid % 8;
		if ((pid_map[byte_index] & (1 << bit_index)) == 0) {
			pid_map[byte_index] |= (1 << bit_index);
			ret = pid;
			break;
		}
	}

	spinlock_unlock_irq_restore(&pid_map_lock, &flags);
	return ret;
}

static void free_pid(pid_t pid) {
	if (pid >= max_pid_count || pid <= 0) {
		printk(PRINTK_ERR "sched: %s bad PID value %i\n", __func__, pid);
		return;
	}

	size_t byte_index = pid / 8;
	unsigned int bit_index = pid % 8;

	unsigned long flags;
	spinlock_lock_irq_save(&pid_map_lock, &flags);
	pid_map[byte_index] &= ~(1 << bit_index);
	spinlock_unlock_irq_restore(&pid_map_lock, &flags);
}

struct proc* sched_proc_alloc(void) {
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

void sched_proc_free(struct proc* proc) {
	slab_cache_free(proc_cache, proc);
	free_pid(proc->pid);
}

struct thread* sched_thread_alloc(void) {
	struct thread* thread = slab_cache_alloc(thread_cache);
	if (!thread)
		return NULL;

	memset(thread, 0, sizeof(*thread));
	return thread;
}

void sched_thread_free(struct thread* thread) {
	slab_cache_free(thread_cache, thread);
}

void sched_create_init(void) {
	proc_cache = slab_cache_create(sizeof(struct proc), _Alignof(struct proc), MM_ZONE_NORMAL, NULL, NULL);
	assert(proc_cache != NULL);
	const size_t pid_map_size = (max_pid_count + 7) / 8;
	pid_map = vmap(NULL, pid_map_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	assert(pid_map != NULL);

	thread_cache = slab_cache_create(sizeof(struct thread), _Alignof(struct thread), MM_ZONE_NORMAL, NULL, NULL);
	assert(thread_cache != NULL);
}
