#pragma once

#include <arch/asm/linkage.h>
#include <lunar/sched_types.h>

#define RFLAGS_DEFAULT 0x202

void __asmlinkage context_switch_generic(struct arch_context* current, struct arch_context* next);
