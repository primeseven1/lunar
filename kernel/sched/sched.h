#pragma once

#include <crescent/compiler.h>
#include <crescent/sched/sched.h>

void sched_create_init(void);
void preempt_init(void);

struct thread* sched_thread_alloc(void);
void sched_thread_free(struct thread* thread);
struct proc* sched_proc_alloc(void);
void sched_proc_free(struct proc* proc);

/**
 * @brief Get the next runnable thread
 *
 * Interrupts must be disabled before calling this function.
 *
 * @return NULL if there is no other runnable thread, otherwise you get a runnable thread
 */
struct thread* get_next_thread(void);
int schedule_thread(struct thread* thread, struct proc* proc, int flags);

__asmlinkage void asm_context_switch(struct context* current, struct context* next);
__asmlinkage _Noreturn void asm_kthread_start(void);
