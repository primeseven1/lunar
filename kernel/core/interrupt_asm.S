#include <crescent/asm/msr.h>

.code64

.text
.globl asm_idt_load
.type asm_idt_load, @function
asm_idt_load:
	subq $16, %rsp

	/* Like with the GDT, the size must be subtracted by 1 */
	testw %si, %si
	jz 1f
	subw $1, %si
1:
	movw %si, (%rsp)
	movq %rdi, 2(%rsp)
	lidt (%rsp)

	addq $16, %rsp
	ret
.size asm_idt_load, . - asm_idt_load

.macro swapgs_if_needed
    cmpq $0x08, 24(%rsp) /* swapgs if interrupt happened in user mode */
    je 1f
    swapgs
1:
.endm

/*
 * Calling isr_common is more convenient for the way i've implemented
 * this. The symbol name will also show up in a stack trace if a panic
 * happens.
 */

/* n is the interrupt number, and msr determines whether or not to do the MSR way of doing swapgs */

.macro isr_no_err n
.text
.type isr_\n, @function
.align 0x10
isr_\n:
	pushq $0
	pushq $\n
	swapgs_if_needed
	cld
	call isr_common
	cli /* make sure no interrupts can happen when handling swapgs */
	swapgs_if_needed
	addq $16, %rsp
	iretq
.size isr_\n, . - isr_\n
.previous
.endm

.macro isr_err n
.text
.type isr_\n, @function
.align 0x10
isr_\n:
	pushq $\n
	swapgs_if_needed
	cld
	call isr_common
	cli /* make sure no interrupts can happen when handling swapgs */
	swapgs_if_needed
	addq $16, %rsp /* skip the error code */
	iretq
.size isr_\n, . - isr_\n
.previous
.endm

.text
.type isr_common, @function
.align 0x10
isr_common:
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

	/* Parameter for _do_isr */
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
.size isr_common, . - isr_common

/* Now just set up all ISR stubs */
.altmacro
.set i, 0
.rept 256
.if ((i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30)
	isr_err %i
.elseif (i == 2 || i == 18)
	isr_no_err %i
.elseif (i == 8)
	isr_err %i
.else
	isr_no_err %i
.endif
	.set i, i+1
.endr

.macro isr_table_define n
.quad isr_\n
.endm

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
