#pragma once

#include <arch/asm/linkage.h>

/**
 * @brief Routine for starting a kernel thread, usually implemented in assembly
 *
 * The first argument points to the function for the kthread at the stack pointer.
 * The next value on the stack is the thread argument as a pointer.
 *
 * This function should never return, and should call kthread_exit() instead.
 */
__asmlinkage void arch_kthread_start(void);
