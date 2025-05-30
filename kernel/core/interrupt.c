#include <crescent/compiler.h>
#include <crescent/core/panic.h>
#include <crescent/core/interrupt.h>
#include "idt.h"

__asmlinkage void __isr_entry(struct context* ctx);
__asmlinkage void __isr_entry(struct context* ctx) {
	panic("ISR: %lu", ctx->int_num);
}

void interrupts_init(void) {
	idt_init();
}
