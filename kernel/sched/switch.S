#include <crescent/asm/segment.h>

.code64

/*
 * struct context {
 * 	void* cr2;
 * 	long rax, rbx, rcx, rdx, rsi, rdi;
 * 	void* rbp;
 *	long r8, r9, r10, r11, r12, r13, r14, r15;
 *	const void* __asm_isr_common_ret;
 *	unsigned long vector, err_code;
 *	void* rip;
 *	unsigned long cs, rflags;
 *	void* rsp;
 *	unsigned long ss;
 * } __attribute__((packed));
 */

.text

/* _Noreturn void asm_task_switch(struct context*) */
.globl asm_task_switch
.type asm_task_switch, @function
asm_task_switch:
	movq %rdi, %rsp /* Contains the context struct */

	popq %rax
	movq %rax, %cr2

	/* If there is any extra context to restore, the C code restores that. */
	popq %rax
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15

	addq $24, %rsp /* Remove __isr_common_ret, vector, and err_code */

	/* Now there's a proper interrupt frame, now just check if returning to user mode */
	cmpq $SEGMENT_USER_CODE, 8(%rsp)
	je .do_iretq
	swapgs
.do_iretq:
	iretq
.size asm_task_switch, . - asm_task_switch
