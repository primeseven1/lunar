#pragma once

#include <lunar/types.h>

struct context;
struct thread_stack;
struct thread;
struct thread_entry_point;

struct arch_context {
	u64 ds, es;
	u64 cr2;
	u64 rax, rbx, rcx, rdx, rsi, rdi;
	u64 rbp;
	u64 r8, r9, r10, r11, r12, r13, r14, r15;
	u64 vector, err_code;
	u64 rip;
	u64 cs, rflags;
	u64 rsp;
	u64 ss;
} __attribute__((packed));

struct arch_x86_64_fxsave_context {
	u16 fcw;
	u16 fsw;
	u8 ftw;
	u8 __reserved0;
	u16 fop;
	u64 fip;
	u64 fdp;
	u32 mxcsr;
	u32 mxcsr_mask;
	u8 st_mm[8][16];
	u8 xmm[16][16];
	u8 __reserved1[96];
} __attribute__((packed, aligned(16)));
static_assert(sizeof(struct arch_x86_64_fxsave_context) == 512);

struct arch_context_extended {
	void* fsbase, *gsbase, *krnlgsbase;
	struct arch_x86_64_fxsave_context* fxsave_region;
};

void arch_context_switch(struct thread* current, struct thread* next);
int arch_context_init(struct context* ctx);
void arch_context_destroy(struct context* ctx);
void arch_thread_prepare_execution(struct thread* thread, const struct thread_entry_point* entry_point, const struct thread_stack* stack);

/**
 * @brief Switch to a new thread, but in an interrupt context
 *
 * The interrupt context is written with the general purpose registers of the new thread after saving them.
 *
 * @param current The current thread
 * @param next The thread to switch to
 * @param intctx The current interrupt context
 */
void arch_x86_64_context_switch_in_interrupt(struct thread* current, struct thread* next, struct arch_context* intctx);
