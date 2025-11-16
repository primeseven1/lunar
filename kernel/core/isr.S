#include <lunar/asm/segment.h>

.code64

/*
 * It's possible that an NMI/MCE happens before swapgs gets executed,
 * the C code should handle this condition properly by reading the MSR directly
 * when an NMI/MCE happens, so when the NMI/MCE returns, GSBASE should be null and
 * the correct value gets swapped.
 */
.macro swapgs_if_needed
	cmpq $SEGMENT_KERNEL_CODE, 24(%rsp)
	je 1f
	swapgs
	lfence
1:
.endm

/* ISR handler with no error code */
.macro isr_no_err n
.text
.type asm_isr_\n, @function
.align 0x10
asm_isr_\n:
	pushq $0
	pushq $\n
	jmp asm_isr_common
.size asm_isr_\n, . - asm_isr_\n
.previous
.endm

/* ISR handler with an error code */
.macro isr_err n
.text
.type asm_isr_\n, @function
.align 0x10
asm_isr_\n:
	pushq $\n
	jmp asm_isr_common
.size asm_isr_\n, . - asm_isr_\n
.previous
.endm

.text
.type isr_common, @function
.align 0x10
asm_isr_common:
	cld

	swapgs_if_needed

	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax
	movq %cr2, %rax
	pushq %rax

	/* struct context* parameter for __isr_entry */
	movq %rsp, %rdi

	/*
	 * Align the stack by 16, save the alignment in rbx since that register
	 * must be preserved throughout function calls (system v abi).
	 */
	movq %rsp, %rbx
	andq $15, %rbx
	subq %rbx, %rsp
	
	/* Fake stack frame */
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	/*
	 * We don't save any extra context, the C code does that if there is any.
	 * The kernel is compiled with -mgeneral-regs-only, so we don't need to worry about
	 * the extra context being trashed.
	 */
	call do_interrupt

	/* Remove fake stack frame and realign the stack */
	leaq 16(%rsp,%rbx), %rsp

	popq %rax
	movq %rax, %cr2
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

	swapgs_if_needed
	addq $16, %rsp /* Remove vector + error code */
	iretq
.size asm_isr_common, . - asm_isr_common

.altmacro
.set i, 0
.rept 256
.if ((i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30 || i == 8)
	isr_err %i
.else
	isr_no_err %i
.endif
	.set i, i+1
.endr

/* No, I could not just inline this without the assembler bitching */
.macro isr_table_define n
.quad asm_isr_\n
.endm

/* With KASLR, there can't be relocations in .rodata */
#ifdef CONFIG_KASLR
.data
#else
.section .rodata
#endif
.globl isr_table
isr_table:
.altmacro
.set i, 0
.rept 256
	isr_table_define %i
	.set i, i+1
.endr
