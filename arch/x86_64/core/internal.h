#pragma once

#include <arch/asm/linkage.h>
#include <lunar/limine.h>
#include <lunar/interrupt.h>

#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

void i8259_spurious_isr(struct isr* isr);
void i8259_initialize_and_mask(void);
__asmlinkage void arch_x86_64_asm_ap_start(struct arch_limine_mp_info* cpu_info);
