#include <lunar/sched.h>
#include <lunar/string.h>
#include <lunar/slab.h>
#include <x86_64/asm/msr.h>
#include "internal.h"

static struct slab_cache* fxsave_cache = NULL;

static inline void fxsave(struct arch_x86_64_fxsave_context* region) {
	__asm__ volatile("fxsave (%0)" : : "r"(region) : "memory");
}

static inline void fxrstor(struct arch_x86_64_fxsave_context* region) {
	__asm__ volatile("fxrstor (%0)" : : "r"(region) : "memory");
}

void arch_context_switch(struct thread* current, struct thread* next) {
	fxsave(current->context.ctx.arch_extended_context.fxsave_region);
	fxrstor(next->context.ctx.arch_extended_context.fxsave_region);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_GS_BASE, (uintptr_t)next->context.ctx.arch_extended_context.gsbase);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_FS_BASE, (uintptr_t)next->context.ctx.arch_extended_context.fsbase);
	context_switch_generic(&current->context.ctx.arch_context, &next->context.ctx.arch_context);
}

void arch_x86_64_context_switch_in_interrupt(struct thread* current, struct thread* next, struct arch_context* intctx) {
	current->context.ctx.arch_context = *intctx;
	fxsave(current->context.ctx.arch_extended_context.fxsave_region);
	*intctx = next->context.ctx.arch_context;
	fxrstor(next->context.ctx.arch_extended_context.fxsave_region);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_GS_BASE, (uintptr_t)next->context.ctx.arch_extended_context.gsbase);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_FS_BASE, (uintptr_t)next->context.ctx.arch_extended_context.fsbase);
}

int arch_thread_context_init(struct thread* thread) {
	struct arch_x86_64_fxsave_context* r;
	if (unlikely(!fxsave_cache)) {
		fxsave_cache = slab_cache_create(sizeof(*r),
				alignof(typeof(*r)), MM_ZONE_NORMAL, NULL, NULL);
		if (unlikely(!fxsave_cache))
			out_of_memory();
	}

	r = slab_cache_alloc(fxsave_cache);
	if (!r)
		return -ENOMEM;

	thread->context.ctx.arch_extended_context.fxsave_region = r;
	thread->context.ctx.arch_extended_context.gsbase = atomic_load(&thread->topology.cpu);
	thread->context.ctx.arch_extended_context.fsbase = NULL;
	memset(&thread->context.ctx.arch_context, 0, sizeof(thread->context.ctx.arch_context));

	return 0;
}

void arch_thread_context_destroy(struct thread* thread) {
	slab_cache_free(fxsave_cache, thread->context.ctx.arch_extended_context.fxsave_region);
}

void arch_thread_prepare_execution(struct thread* thread, void (*exec)(void), void* stack, bool kernel) {
	u64 cs = ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3;
	u64 ds = ARCH_X86_64_SEGMENT_USER_DATA | ARCH_X86_64_SEGMENT_CPL3;
	if (kernel) {
		cs = ARCH_X86_64_SEGMENT_KERNEL_CODE | ARCH_X86_64_SEGMENT_CPL0;
		ds = ARCH_X86_64_SEGMENT_KERNEL_DATA | ARCH_X86_64_SEGMENT_CPL0;
	}
	thread->context.ctx.arch_context = (struct arch_context){
		.rip = (uintptr_t)exec,
		.rflags = 0x202,
		.rsp = (uintptr_t)stack,
		.cs = cs,
		.ss = ds,
		.ds = ds,
		.es = ds
	};
}
