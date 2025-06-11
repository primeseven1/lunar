.code64
.text

.globl _start
.type _start, @function
_start:
	/* Make sure interrupts and the direction flag are both disabled */
	cli
	cld

	/* Zero the BSS section */
#ifdef CONFIG_KASLR
	leaq _ld_kernel_bss_start(%rip), %rdi /* Destination parameter */
	leaq _ld_kernel_bss_end(%rip), %rdx
#else
	leaq _ld_kernel_bss_start, %rdi /* Destination parameter */
	leaq _ld_kernel_bss_end, %rdx
#endif
	xorq %rsi, %rsi /* Value paramter */
	subq %rdi, %rdx /* Size parameter */
	call memset

	/* Align the stack and call the C entry point */
	andq $-16, %rsp
	callq kernel_main

	/* kernel_main should never return, so this should never get executed */
	ud2
.size _start, . - _start
