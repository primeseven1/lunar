#pragma once

#include <lunar/core/mutex.h>
#include <lunar/sched/scheduler.h>

struct completion {
	atomic(bool) done;
	struct list_head waiters;
	spinlock_t lock;
};

#define COMPLETION_DEFINE(n) struct completion n = { .done = false, .waiters = LIST_HEAD_INITIALIZER(n.waiters), .lock = SPINLOCK_INITIALIZER }

static inline void completion_init(struct completion* completion) {
	atomic_store_explicit(&completion->done, false, ATOMIC_RELAXED);
	list_head_init(&completion->waiters);
	spinlock_init(&completion->lock);
}

int completion_wait(struct completion* completion, int flags);
void completion_complete(struct completion* completion);
void completion_reset(struct completion* completion);
