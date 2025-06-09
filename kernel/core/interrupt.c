#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/flags.h>
#include <crescent/core/panic.h>
#include <crescent/core/locking.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/interrupt.h>
#include <crescent/lib/string.h>
#include "idt.h"
#include "traps.h"

static struct isr isr_handlers[INTERRUPT_COUNT];

const struct isr* interrupt_register(void (*handler)(const struct isr*, const struct context*), void (*eoi)(const struct isr*)) {
	unsigned long irq = local_irq_save();

	struct isr* ret = NULL;
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (!isr_handlers[i].handler) {
			ret = &isr_handlers[i];
			ret->handler = handler;
			ret->eoi = eoi;
			break;
		}
	}

	local_irq_restore(irq);
	return ret;
}

__asmlinkage void __isr_entry(const struct context* ctx);
__asmlinkage void __isr_entry(const struct context* ctx) {
	current_cpu()->in_interrupt = true;

	/* This should never happen */
	if (unlikely(ctx->int_num >= INTERRUPT_COUNT))
		panic("Cannot handle interrupt: ctx->int_num == %lu", ctx->int_num);

	struct isr* isr = &isr_handlers[ctx->int_num];
	if (!isr->handler) {
		printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", 
				ctx->int_num);
	} else {
		isr->handler(isr, ctx);
	}

	if (isr->eoi)
		isr->eoi(isr);

	current_cpu()->in_interrupt = false;
}

static void nmi(const struct isr* isr, const struct context* ctx) {
	(void)isr;
	(void)ctx;
	panic("NMI");
}

static void spurious(const struct isr* isr, const struct context* ctx) {
	(void)isr;
	(void)ctx;
}

void interrupts_init(void) {
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		isr_handlers[i].int_num = i;
		isr_handlers[i].eoi = NULL;
		if (i == INTERRUPT_SPURIOUS_VECTOR)
			isr_handlers[i].handler = spurious;
		else if (i < INTERRUPT_EXCEPTION_COUNT)
			isr_handlers[i].handler = i == INTERRUPT_EXCEPTION_NMI ? nmi : do_trap;
		else
			isr_handlers[i].handler = NULL;
	}

	idt_init();
}
