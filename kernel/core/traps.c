#include <crescent/common.h>
#include <crescent/core/panic.h>
#include "traps.h"

static void do_page_fault(const struct context* ctx) {
	if (ctx->cr2 == NULL)
		panic("null pointer dereference at rip: %p", ctx->rip);

	panic("page fault at rip: %p, cr2: %p", ctx->rip, ctx->cr2);
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
