#pragma once

struct context {
	void* cr2;
	long rax, rbx, rcx, rdx, rsi, rdi;
	void* rbp;
	long r8, r9, r10, r11, r12, r13, r14, r15;
	const void* __asm_isr_common_ret;
	long int_num, err_code;
	void* rip;
	long cs, rflags;
	void* rsp;
	long ss;
} __attribute__((packed));

#define INTERRUPT_COUNT 256

void interrupts_init(void);
