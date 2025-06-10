#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/core/panic.h>
#include <crescent/core/locking.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/i8259.h>
#include <crescent/core/interrupt.h>
#include <crescent/lib/string.h>
#include "idt.h"
#include "traps.h"

static struct isr isr_handlers[INTERRUPT_COUNT];
static spinlock_t isr_handlers_lock = SPINLOCK_INITIALIZER;

const struct isr* interrupt_register(void (*handler)(const struct isr*, const struct context*), void (*eoi)(const struct isr*)) {
	unsigned long flags;
	spinlock_lock_irq_save(&isr_handlers_lock, &flags);

	struct isr* ret = NULL;
	for (int i = 0; i < INTERRUPT_COUNT; i++) {
		if (!isr_handlers[i].handler) {
			ret = &isr_handlers[i];
			ret->handler = handler;
			ret->eoi = eoi;
			break;
		}
	}

	spinlock_unlock_irq_restore(&isr_handlers_lock, &flags);
	return ret;
}

__asmlinkage void __isr_entry(const struct context* ctx);
__asmlinkage void __isr_entry(const struct context* ctx) {
	bool previous = current_cpu()->in_interrupt;
	if (!previous)
		current_cpu()->in_interrupt = true;

	/* This should never happen */
	if (unlikely(ctx->vector >= INTERRUPT_COUNT))
		panic("Cannot handle interrupt: ctx->vector == %lu", ctx->vector);

	struct isr* isr = &isr_handlers[ctx->vector];
	if (likely(isr->handler))
		isr->handler(isr, ctx);
	else
		printk(PRINTK_CRIT "core: Interrupt %lu happened, there is no handler for it!\n", ctx->vector);

	if (isr->eoi)
		isr->eoi(isr);

	current_cpu()->in_interrupt = previous;
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
		isr_handlers[i].vector = i;
		isr_handlers[i].eoi = NULL;
		if (i < INTERRUPT_EXCEPTION_COUNT) {
			isr_handlers[i].handler = i == INTERRUPT_EXCEPTION_NMI ? nmi : do_trap;
		} else if (i >= I8259_VECTOR_OFFSET && i < I8259_VECTOR_OFFSET + I8259_VECTOR_COUNT) {
			isr_handlers[i].handler = spurious;
			isr_handlers[i].eoi = i8259_spurious_eoi;
		} else if (i == INTERRUPT_SPURIOUS_VECTOR) {
			isr_handlers[i].handler = spurious;
		} else {
			isr_handlers[i].handler = NULL;
		}
	}

	idt_init();
}
