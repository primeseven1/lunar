#pragma once

#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>

/**
 * @brief Initialize the ability to do stack traces
 *
 * @retval 0 Success
 * @retval -ENOPROTOOPT Limine kernel file request not available
 * @retval -ENOENT Kernel symbol table not available
 */
int stack_tracer_init(void);

/**
 * @brief Prints the registers for a context
 */
void dump_registers(const struct context* ctx);

/**
 * @brief Print a stack trace for the current CPU
 */
void dump_stack(void);
