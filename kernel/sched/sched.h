#pragma once

#include <crescent/sched/sched.h>

void sched_proc_init(void);
void sched_thread_init(void);
void sched_lapic_timer_init(void);

proc_t* sched_proc_alloc(void);
void sched_proc_free(proc_t* proc);
thread_t* sched_thread_alloc(void);
void sched_thread_free(thread_t* thread);

int sched_schedule_new_thread(thread_t* thread, proc_t* proc, unsigned int flags);

/**
 * @brief Switch a task from a timer interrupt
 *
 * This function allows the interrupt handler to restore the general purpose registers,
 * this allows for the EOI to be issued automatically
 *
 * @param ctx The context from the interrupt handler
 */
void sched_switch_from_interrupt(struct context* ctx);
