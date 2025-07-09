#pragma once

#include <crescent/sched/types.h>

thread_t* kthread_create(unsigned int flags, void* (*func)(void*), void* arg);
_Noreturn void kthread_exit(void* ret);
void* kthread_join(thread_t* thread);
