#pragma once

#include <crescent/sched/scheduler.h>

enum kthread_flags {
	KTHREAD_JOIN = (1 << 0)
};

struct thread* kthread_create(int sched_flags, void* (*func)(void*), void* arg);
_Noreturn void kthread_exit(void* ret);
void* kthread_join(struct thread* thread);
