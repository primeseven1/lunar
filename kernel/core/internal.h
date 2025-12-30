#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/interrupt.h>

#define I8259_VECTOR_OFFSET 0x20
#define I8259_VECTOR_COUNT 0x10

void i8259_disable(void);
void i8259_spurious_isr(struct isr* isr, struct context* ctx);
