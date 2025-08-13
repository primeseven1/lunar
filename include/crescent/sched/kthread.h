#pragma once

#include <crescent/sched/scheduler.h>

struct thread* kthread_create(int sched_flags, void* (*func)(void*), void* arg);

static inline void kthread_detach(struct thread* thread) {
	atomic_sub_fetch(&thread->refcount, 1, ATOMIC_RELEASE);
}

_Noreturn void kthread_exit(void* ret);
void* kthread_join(struct thread* thread);
