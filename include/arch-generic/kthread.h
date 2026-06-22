#pragma once

#include <arch/asm/linkage.h>

/**
 * @brief Assembly routine for starting a kthread
 *
 * The argument and function to run are placed on the stack.
 * The function should be popped of the stack first, then the argument is next.
 */
void __asmlinkage arch_asm_kthread_start(void);
