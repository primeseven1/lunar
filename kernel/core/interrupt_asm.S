#include <crescent/asm/segment.h>

.macro swapgs_if_needed
	cmpq $SEGMENT_KERNEL_CODE, 24(%rsp) /* Check to see if the interrupt happened in kernel mode */
	je 1f
	swapgs /* Only gets executed if the interrupt happened in user mode */
1:
.endm

/*
 * Calling isr_common is more convenient for the way i've implemented
 * this. The symbol name will also show up in a stack trace if a panic
 * happens.
 */

/* ISR handler with no error code */
.macro isr_no_err n
.text
.type asm_isr_\n, @function
.align 0x10
asm_isr_\n:
	pushq $0
	pushq $\n
	swapgs_if_needed
	cld
	call asm_isr_common
	cli /* make sure no interrupts can happen when handling swapgs, iretq will restore this */
	swapgs_if_needed
	addq $16, %rsp
	iretq
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
	swapgs_if_needed
	cld
	call asm_isr_common
	cli /* make sure no interrupts can happen when handling swapgs, iretq will restore this */
	swapgs_if_needed
	addq $16, %rsp /* skip the error code */
	iretq
.size asm_isr_\n, . - asm_isr_\n
.previous
.endm

.code64
.text
.type isr_common, @function
.align 0x10
asm_isr_common:
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

	/* Parameter for __isr_entry */
	movq %rsp, %rdi

	movq %rsp, %rbx
	andq $15, %rbx
	subq %rbx, %rsp

	call __isr_entry

	addq %rsp, %rbx

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

	ret
.size asm_isr_common, . - asm_isr_common

/* Now just set up all ISR stubs */
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
