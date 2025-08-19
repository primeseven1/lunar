#include <crescent/asm/cpuid.h>
#include <crescent/asm/ctl.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/sched/scheduler.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>
#include "internal.h"

static bool use_fxsave = false;

void context_switch(struct thread* next) {
	unsigned long irq = local_irq_save();

	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->runqueue.current;
	if (likely(use_fxsave))
		__asm__ volatile("fxsave %0" : : "m"(*(u8*)current->ctx.extended) : "memory");
	cpu->runqueue.current = next;
	asm_context_switch(&current->ctx.general, &next->ctx.general);
	if (likely(use_fxsave))
		__asm__ volatile("fxrstor %0" : : "m"(*(u8*)current->ctx.extended) : "memory");

	local_irq_restore(irq);
}

static struct slab_cache* ext_ctx_cache = NULL;

void* ext_ctx_alloc(void) {
	if (likely(ext_ctx_cache))
		return slab_cache_alloc(ext_ctx_cache);
	return (void*)-1;
}

void ext_ctx_free(void* ptr) {
	if (unlikely(ptr == (void*)-1))
		return;
	slab_cache_free(ext_ctx_cache, ptr);
}

static void ext_ctx_ctor(void* obj) {
	memset(obj, 0, ext_ctx_cache->obj_size);
}

static void enable_sse(void) {
	unsigned long ctl = ctl0_read();
	ctl &= ~CTL0_EM;
	ctl |= CTL0_MP;
	ctl0_write(ctl);
	ctl = ctl4_read();
	ctl |= CTL4_OSFXSR | CTL4_OSXMMEXCEPT;
	ctl4_write(ctl);

	u32 mxcsr = 0x1F80;
	__asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
}

void ext_context_init(void) {
	u32 edx, _unused;
	cpuid(1, 0, &_unused, &_unused, &_unused, &edx);
	if (likely(edx & (1 << 25))) {
		use_fxsave = true;
		ext_ctx_cache = slab_cache_create(512, 16, MM_ZONE_NORMAL, ext_ctx_ctor, NULL);
		assert(ext_ctx_cache != NULL);
		enable_sse();
	}
}
