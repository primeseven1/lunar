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

static u8* pid_map = NULL;
static spinlock_t pid_map_lock = SPINLOCK_STATIC_INITIALIZER;
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

static void proc_ctor(void* obj) {
	proc_t* proc = obj;

	proc->pid = alloc_pid();
	proc->threadinfo.threads = NULL;
	proc->threadinfo.thread_count = 0;
	atomic_store(&proc->threadinfo.lock, SPINLOCK_INITIALIZER, ATOMIC_RELAXED);
	proc->vmm_ctx = NULL;
	proc->parent = NULL;
	proc->sibling = NULL;
	proc->child = NULL;
}

static void proc_dtor(void* obj) {
	proc_t* proc = obj;
	if (proc->pid != max_pid_count)
		free_pid(proc->pid);
}

proc_t* sched_proc_alloc(void) {
	proc_t* proc = slab_cache_alloc(proc_cache);
	if (!proc)
		return NULL;
	if (proc->pid == max_pid_count) {
		slab_cache_free(proc_cache, proc);
		return NULL;
	}

	return proc;
}

void sched_proc_free(proc_t* proc) {
	slab_cache_free(proc_cache, proc);
}

void sched_proc_init(void) {
	proc_cache = slab_cache_create(sizeof(proc_t), _Alignof(proc_t), MM_ZONE_NORMAL, proc_ctor, proc_dtor);
	assert(proc_cache != NULL);

	const size_t pid_map_size = (max_pid_count + 7) / 8;
	pid_map = vmap(NULL, pid_map_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	assert(pid_map != NULL);
	memset(pid_map, 0, pid_map_size);
}
