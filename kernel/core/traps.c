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

	/* Prevent the compiler from making a jump instead of a call */
	__asm__ volatile("ud2");
}

void do_trap(const struct isr* isr, const struct context* ctx) {
	switch (isr->int_num) {
	case 14:
		do_page_fault(ctx);
		break;
	default:
		panic("Unhandled Exception: %lu", ctx->int_num);
	}
}
