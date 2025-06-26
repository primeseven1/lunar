#pragma once

#include <crescent/sched/types.h>

struct thread* kthread_create(unsigned int flags, void* (*func)(void*), void* arg);
_Noreturn void kthread_exit(void* ret);
void* kthread_join(struct thread* thread);
