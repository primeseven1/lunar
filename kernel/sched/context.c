#include <crescent/core/panic.h>
#include <crescent/asm/ctl.h>
#include <crescent/asm/cpuid.h>
#include <crescent/mm/slab.h>
#include <crescent/lib/string.h>
#include "internal.h"

static bool fxsave = false;

void context_switch(struct thread* prev, struct thread* next) {
	if (fxsave)
		__asm__ volatile("fxsave (%0)" : : "r"(prev->ctx.extended) : "memory");
	asm_context_switch(&prev->ctx.general, &next->ctx.general);
	if (fxsave)
		__asm__ volatile("fxrstor (%0)" : : "r"(prev->ctx.extended) : "memory");
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
		fxsave = true;
		ext_ctx_cache = slab_cache_create(512, 16, MM_ZONE_NORMAL, ext_ctx_ctor, NULL);
		assert(ext_ctx_cache != NULL);
		enable_sse();
	}
}
