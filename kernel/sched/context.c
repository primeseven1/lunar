#include <lunar/core/panic.h>
#include <lunar/core/cpu.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/wrap.h>
#include <lunar/mm/slab.h>
#include <lunar/lib/string.h>
#include <lunar/sched/scheduler.h>
#include "internal.h"

void atomic_context_switch(struct thread* prev, struct thread* next, struct context* ctx) {
	prev->ctx.general = *ctx;
	cpu_fxsave(prev->ctx.extended);
	*ctx = next->ctx.general;
	current_cpu()->tss->rsp[0] = next->utk_stack;
	cpu_fxrstor(next->ctx.extended);
}

void context_switch(struct thread* prev, struct thread* next) {
	cpu_fxsave(prev->ctx.extended);
	current_cpu()->tss->rsp[0] = next->utk_stack;
	asm_context_switch(&prev->ctx.general, &next->ctx.general);
	cpu_fxrstor(prev->ctx.extended);
}

static struct slab_cache* ext_ctx_cache = NULL;

void* ext_ctx_alloc(void) {
	return slab_cache_alloc(ext_ctx_cache);
}

void ext_ctx_free(void* ptr) {
	slab_cache_free(ext_ctx_cache, ptr);
}

void ext_context_init(void) {
	ext_ctx_cache = slab_cache_create(512, 16, MM_ZONE_NORMAL, NULL, NULL);
	if (!ext_ctx_cache)
		panic("ext_context_init() failed!");
}
