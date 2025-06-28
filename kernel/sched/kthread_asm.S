.code64

.text
.globl asm_kthread_start
.type asm_kthread_start, @function
asm_kthread_start:
	andq $-16, %rsp

	pushq $0 /* Fake return address */
	pushq $0 /* Fake stack frame */

	/* The thread function and thread arguments are already in rdi/rsi at this point */
	call __kthread_start
	ud2
.size asm_kthread_start, . - asm_kthread_start
