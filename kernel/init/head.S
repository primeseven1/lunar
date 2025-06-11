.code64
.text

.type asm_zero_bss, @function
asm_zero_bss:
#ifdef CONFIG_KASLR
	leaq _ld_kernel_bss_start(%rip), %rdi
	leaq _ld_kernel_bss_end(%rip), %rcx
#else
	leaq _ld_kernel_bss_start, %rdi /* Address for stosq/stosb */
	leaq _ld_kernel_bss_end, %rcx /* Will be used as the byte count after subtracting with rdi */
#endif /* CONFIG_KASLR */
	subq %rdi, %rcx /* Get the byte count */
	movq %rcx, %rdx /* Save the original number of bytes */
	xorq %rax, %rax /* The value to store (0) */

	/* Now zero out the memory 64 bits at a time */
	shrq $3, %rcx /* Divide byte count by 8 to get the number of qwords */
	rep stosq

	/* Now zero out the remaining bytes */
	movq %rdx, %rcx /* Get back the original number of bytes */
	andq $7, %rcx /* Now get the remainder of rcx when dividing by 8 */
	rep stosb
	ret
.size asm_zero_bss, . - asm_zero_bss

.globl _start
.type _start, @function
_start:
	/* Make sure interrupts and the direction flag are both disabled */
	cli
	cld

	/* Align the stack */
	andq $-16, %rsp
	call asm_zero_bss
	call kernel_main

	/* kernel_main should never return, so this should never get executed */
	ud2
.size _start, . - _start
