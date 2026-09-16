#pragma once

#include <lunar/limine.h>
#include <lunar/interrupt.h>
#include <arch/asm/linkage.h>
#include <x86_64/asm/flags.h>

#define RFLAGS_DEFAULT (ARCH_X86_64_RFLAGS_IF | ARCH_X86_64_RFLAGS_RSVD1_SET)
#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

void i8259_initialize_and_mask(void);
__asmlinkage void ap_start(struct arch_limine_mp_info* cpu_info);

/**
 * @brief Do a context switch with the general purpose registers
 *
 * @param current The current thread context pointer
 * @param next The context to switch to
 */
__asmlinkage void context_switch_gp_regs(struct arch_context* current, struct arch_context* next);
