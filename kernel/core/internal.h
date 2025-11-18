#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/interrupt.h>

#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

void dispatch_trap(struct isr* isr, struct context* ctx);

int i8259_set_irq(struct isr* isr, int irq, struct cpu* cpu, bool masked);
void i8259_init(void);
