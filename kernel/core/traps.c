#include <crescent/common.h>
#include <crescent/asm/segment.h>
#include <crescent/core/panic.h>
#include <crescent/core/trace.h>
#include "traps.h"

static void do_page_fault(const struct context* ctx) {
	if (ctx->cs == SEGMENT_KERNEL_CODE) {
		dump_registers(ctx);
		if (!ctx->cr2)
			panic("NULL pointer dereference at rip: %p", ctx->rip);

		panic("kernel page fault");
	}

	/* Make sure a valid return address is printed in a stack trace */
	__asm__ volatile("ud2");
}

void do_trap(const struct isr* isr, struct context* ctx) {
	switch (isr->vector) {
	case INTERRUPT_PAGE_FAULT_VECTOR:
		do_page_fault(ctx);
		break;
	default:
		panic("Unhandled Exception: %lu", ctx->vector);
	}
}
