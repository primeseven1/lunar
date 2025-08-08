#pragma once

#include <crescent/lib/list.h>
#include <crescent/core/spinlock.h>
#include <crescent/core/timekeeper.h>

struct semaphore {
	long count;
	struct list_head wait_queue;
	spinlock_t lock;
};

static inline void semaphore_init(struct semaphore* sem, long count) {
	sem->count = count;
	list_head_init(&sem->wait_queue);
	spinlock_init(&sem->lock);
}

void semaphore_wait(struct semaphore* sem);
int semaphore_wait_timed(struct semaphore* sem, time_t timeout_ms);
void semaphore_signal(struct semaphore* sem);
bool semaphore_try(struct semaphore* sem);
