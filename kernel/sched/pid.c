#include <lunar/init/status.h>
#include <lunar/mm/heap.h>
#include <lunar/sched/scheduler.h>
#include <lunar/lib/string.h>
#include "internal.h"

static u8* pid_map = NULL;
static SPINLOCK_DEFINE(pid_lock);
static const pid_t pid_max = 0x10000;
static const tid_t tid_max = 0x10000;

static inline void id_lock(spinlock_t* lock) {
	if (init_status_get() >= INIT_STATUS_SCHED)
		spinlock_lock_preempt_disable(lock);
	else
		spinlock_lock(lock);
}

static inline void id_unlock(spinlock_t* lock) {
	if (init_status_get() >= INIT_STATUS_SCHED)
		spinlock_unlock_preempt_enable(lock);
	else
		spinlock_unlock(lock);
}

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

pid_t pid_alloc(void) {
	if (unlikely(!pid_map)) {
		size_t size = (pid_max + 7) >> 3;
		pid_map = vmalloc(size);
		if (!pid_map)
			panic("Failed to allocate PID bitmap\n");
	}

	id_lock(&pid_lock);
	pid_t id = alloc_id(pid_map, pid_max);
	id_unlock(&pid_lock);
	return id != pid_max ? id : -1;
}

int pid_free(pid_t id) {
	if (id >= pid_max)
		return -EINVAL;

	id_lock(&pid_lock);
	free_id(pid_map, id);
	id_unlock(&pid_lock);

	return 0;
}

int tid_create_bitmap(struct proc* proc) {
	proc->tid_map = vmalloc((tid_max + 7) >> 3);
	if (!proc->tid_map)
		return -ENOMEM;
	return 0;
}

void tid_free_bitmap(struct proc* proc) {
	vfree(proc->tid_map);
}

tid_t tid_alloc(struct proc* proc) {
	id_lock(&proc->tid_lock);
	tid_t id = alloc_id(proc->tid_map, tid_max);
	id_unlock(&proc->tid_lock);
	return id != tid_max ? id : -1;
}

int tid_free(struct proc* proc, tid_t id) {
	if (id >= tid_max)
		return -EINVAL;

	id_lock(&proc->tid_lock);
	free_id(proc->tid_map, id);
	id_unlock(&proc->tid_lock);

	return 0;
}
