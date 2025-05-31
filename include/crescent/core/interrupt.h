#pragma once

#include <crescent/core/locking.h>

struct context {
	void* cr2;
	long rax, rbx, rcx, rdx, rsi, rdi;
	void* rbp;
	long r8, r9, r10, r11, r12, r13, r14, r15;
	const void* __asm_isr_common_ret;
	unsigned long int_num, err_code;
	void* rip;
	unsigned long cs, rflags;
	void* rsp;
	unsigned long ss;
} __attribute__((packed));

struct isr {
	void (*handler)(const struct isr* self, const struct context* ctx);
	void (*eoi)(const struct isr* self);
	unsigned long int_num;
	spinlock_t lock;
};

#define INTERRUPT_COUNT 256

const struct isr* interrupt_register(void (*handler)(const struct isr*, const struct context*));
void interrupts_init(void);
