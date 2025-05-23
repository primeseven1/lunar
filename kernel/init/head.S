.code64
.text

.globl _start
.type _start, @function
_start:
	/* Make sure interrupts and the direction flag are both disabled */
	cli
	cld

	/* Align the stack and call the C entry point */
	andq $-16, %rsp
	callq kernel_main

	/* kernel_main should never return, so this should never get executed */
	ud2
.size _start, . - _start
