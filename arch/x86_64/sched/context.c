#include <lunar/sched.h>
#include <lunar/proc.h>
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

static void save_extended(struct arch_context_extended* region) {
	region->gsbase = (void*)(uintptr_t)arch_x86_64_rdmsr(ARCH_X86_64_MSR_GS_BASE);
	region->krnlgsbase = (void*)(uintptr_t)arch_x86_64_rdmsr(ARCH_X86_64_MSR_KERNEL_GS_BASE);
	region->fsbase = (void*)(uintptr_t)arch_x86_64_rdmsr(ARCH_X86_64_MSR_FS_BASE);
	fxsave(region->fxsave_region);
}

static void restore_extended(struct arch_context_extended* region) {
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_GS_BASE, (uintptr_t)region->gsbase);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_KERNEL_GS_BASE, (uintptr_t)region->krnlgsbase);
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_FS_BASE, (uintptr_t)region->fsbase);
	fxrstor(region->fxsave_region);
}

void arch_context_switch(struct thread* current, struct thread* next) {
	save_extended(&current->context.arch_extended_context);
	restore_extended(&next->context.arch_extended_context);
	current_cpu()->arch_specific.tss.rsp[0] = (next->stack.user_stack_top) ? next->stack.kernel_stack_top : NULL; /* Checking user_stack_top checks if it's a user thread */
	context_switch_generic(&current->context.arch_context, &next->context.arch_context);
}

void arch_x86_64_context_switch_in_interrupt(struct thread* current, struct thread* next, struct arch_context* intctx) {
	save_extended(&current->context.arch_extended_context);
	restore_extended(&next->context.arch_extended_context);
	current_cpu()->arch_specific.tss.rsp[0] = (next->stack.user_stack_top) ? next->stack.kernel_stack_top : NULL; /* Checking user_stack_top checks if it's a user thread */
	current->context.arch_context = *intctx;
	*intctx = next->context.arch_context;
}

int arch_context_init(struct context* context) {
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

	context->arch_extended_context.fxsave_region = r;
	context->arch_extended_context.gsbase = NULL;
	context->arch_extended_context.fsbase = NULL;
	memset(&context->arch_context, 0, sizeof(context->arch_context));

	return 0;
}

void arch_context_destroy(struct context* ctx) {
	slab_cache_free(fxsave_cache, ctx->arch_extended_context.fxsave_region);
}

void arch_thread_prepare_execution(struct thread* thread, const struct thread_entry_point* entry_point) {
	struct context* const ctx = &thread->context;
	struct thread_stack* const stack = &thread->stack;

	uintptr_t entry, stack_top, stack_off;
	u64 cs, ds;
	if (entry_point->user_entry) {
		bug(entry_point->kernel_entry != NULL);
		bug(stack->kernel_stack_top == NULL);
		entry = (uintptr_t)entry_point->user_entry;
		stack_top = (uintptr_t)stack->user_stack_top;
		stack_off = (uintptr_t)stack->user_ptr_off;
		cs = ARCH_X86_64_SEGMENT_USER_CODE | ARCH_X86_64_SEGMENT_CPL3;
		ds = ARCH_X86_64_SEGMENT_USER_DATA | ARCH_X86_64_SEGMENT_CPL3;
	} else {
		bug(entry_point->user_entry != NULL);
		bug(stack->user_stack_top != NULL);
		entry = (uintptr_t)entry_point->kernel_entry;
		stack_top = (uintptr_t)stack->kernel_stack_top;
		stack_off = (uintptr_t)stack->kernel_ptr_off;
		cs = ARCH_X86_64_SEGMENT_KERNEL_CODE | ARCH_X86_64_SEGMENT_CPL0;
		ds = ARCH_X86_64_SEGMENT_KERNEL_DATA | ARCH_X86_64_SEGMENT_CPL0;
		ctx->arch_extended_context.gsbase = atomic_load(&thread->topology.cpu);
	}

	/* Now set up the interrupt frame */
	ctx->arch_context.ds = ds;
	ctx->arch_context.es = ds;
	ctx->arch_context.rip = entry;
	ctx->arch_context.cs = cs;
	ctx->arch_context.rflags = RFLAGS_DEFAULT;
	ctx->arch_context.rsp = stack_top - stack_off;
	ctx->arch_context.ss = ds;
}
