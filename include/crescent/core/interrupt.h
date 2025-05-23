#pragma once

#include <crescent/types.h>

struct ctx {
	void* cr2;
	u64 rax;
	u64 rbx;
	u64 rcx;
	u64 rdx;
	u64 rsi;
	u64 rdi;
	void* rbp;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
	const void* __isr_common_ret;
	u64 int_num;
	u64 err_code;
	void* rip;
	u64 cs;
	u64 rflags;
	void* rsp;
	u64 ss;
} __attribute__((packed));

void interrupts_init(void);
