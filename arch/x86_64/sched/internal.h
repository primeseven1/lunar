#pragma once

#include <arch/asm/linkage.h>
#include <x86_64/asm/flags.h>
#include <lunar/sched_types.h>

#define RFLAGS_DEFAULT (ARCH_X86_64_RFLAGS_IF | ARCH_X86_64_RFLAGS_RSVD1_SET)

void __asmlinkage context_switch_generic(struct arch_context* current, struct arch_context* next);
