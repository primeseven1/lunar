#pragma once

struct context;
struct thread_stack;
struct thread;
struct thread_entry_point;

/**
 * @brief Do a context switch
 *
 * IRQ's are off when this function is called.
 * 
 * @param current The current thread
 * @param next The thread to switch to
 */
void arch_context_switch(struct thread* current, struct thread* next);

/**
 * @brief Initialize a context structure
 * @return -errno on failure, 0 on success
 */
int arch_context_init(struct context* ctx);

/**
 * @brief Destroy a context
 * @param ctx The context to destroy
 */
void arch_context_destroy(struct context* ctx);

/**
 * @brief Prepare a thread for execution
 *
 * The thread stack is placed in thread->stack
 *
 * @param thread The thread to prepare
 * @param entry_point Where the thread should start executing
 */
void arch_thread_prepare_execution(struct thread* thread, const struct thread_entry_point* entry_point);
