#include <crescent/asm/segment.h>

.text
.globl asm_gdt_load
.type asm_gdt_load, @function
asm_gdt_load:
	subq $16, %rsp

	testw %si, %si
	jz 1f
	subw $1, %si
1:
	movw %si, (%rsp)
	movq %rdi, 2(%rsp)
	lgdt (%rsp)

	addq $16, %rsp

	swapgs /* The GSBASE register contains the per-cpu data, so swap it */
	movw $0, %ax
	movw %ax, %gs
	movw %ax, %fs
	swapgs

	movw $SEGMENT_KERNEL_DATA, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss

	pushq $SEGMENT_KERNEL_CODE
	leaq .reload(%rip), %rax
	pushq %rax
	lretq
.reload:
	movw $SEGMENT_TASK_STATE, %ax
	ltr %ax
	ret
.size asm_gdt_load, . - asm_gdt_load
