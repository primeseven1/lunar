.code64

.text
.globl asm_kthread_start
.type asm_kthread_start, @function
asm_kthread_start:
	andq $-16, %rsp

	/* Fake frame */
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	/* rdi/rsi already has the function/argument */
	call __kthread_start
	ud2
.size asm_kthread_start, . - asm_kthread_start
