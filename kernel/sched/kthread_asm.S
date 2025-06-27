.code64

.text
.globl asm_kthread_start
asm_kthread_start:
	andq $-16, %rsp

	pushq $0 /* Fake return address */
	pushq $0 /* Fake stack frame */
	call *%rax

	ud2 /* kthread_exit should be called instead, so trap here */
