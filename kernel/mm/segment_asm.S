#include <crescent/asm/segment.h>

.text
.globl asm_gdt_load
.type asm_gdt_load, @function
asm_gdt_load:
	subq $16, %rsp

	/* 
	 * Since the limit field must be subtracted by 1, 
	 * check if it's zero first to prevent overflow 
	 */
	testw %si, %si
	jz 1f
	subw $1, %si
1:
	/* 
	 * Now setup the GDT struct as this on the stack:
	 * struct {
	 *	u16 limit;
	 *	void* gdtp;
	 * } __attribute((packed));
	 */
	movw %si, (%rsp)
	movq %rdi, 2(%rsp)
	lgdt (%rsp)

	addq $16, %rsp

	/* The GSBASE register contains the per-cpu data, so swap it */
	swapgs
	movw $0, %ax
	movw %ax, %gs
	movw %ax, %fs
	swapgs

	/* Tell the CPU the offset the data segment is in the GDT */
	movw $SEGMENT_KERNEL_DATA, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss

	/* Set up the stack for the far return to the code segment */
	pushq $SEGMENT_KERNEL_CODE
	leaq .reload(%rip), %rax
	pushq %rax
	lretq
.reload:
	/* Tell the CPU what offset the TSS segment is in the GDT */
	movw $SEGMENT_TASK_STATE, %ax
	ltr %ax
	ret
.size asm_gdt_load, . - asm_gdt_load
