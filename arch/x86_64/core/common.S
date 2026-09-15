#include <arch/asm/linkage.h>
#include <x86_64/asm/segment.h>

.code64
.text

/* __asmlinkage void arch_x86_64_gdt_reload(const struct arch_x86_64_gdt* gdt, size_t size) */
ASM_FUNCTION_START(arch_x86_64_gdt_reload, ASM_LINKAGE_GLOBAL):
	subq $16, %rsp

	dec %si
	movw %si, (%rsp)
	movq %rdi, 2(%rsp)
	lgdt (%rsp)

	xorq %rax, %rax
	swapgs
	movq %rax, %gs
	movq %rax, %fs
	swapgs

	movq $ARCH_X86_64_SEGMENT_KERNEL_DATA, %rax
	movq %rax, %ds
	movq %rax, %ss
	movq %rax, %es

	leaq .reload(%rip), %rax
	pushq $ARCH_X86_64_SEGMENT_KERNEL_CODE
	pushq %rax
	lretq
.reload:
	movw $ARCH_X86_64_SEGMENT_TASK_STATE, %ax
	ltr %ax
	addq $16, %rsp
	ret
ASM_FUNCTION_END(arch_x86_64_gdt_reload)

/* __asmlinkage void arch_x86_64_idt_reload(const struct arch_x86_64_idt* idt, size_t size) */
ASM_FUNCTION_START(arch_x86_64_idt_reload, ASM_LINKAGE_GLOBAL):
	subq $16, %rsp

	dec %si
	movw %si, (%rsp)
	movq %rdi, 2(%rsp)
	lidt (%rsp)

	addq $16, %rsp
	ret
ASM_FUNCTION_END(arch_x86_64_idt_reload)

/* __asmlinkage void arch_kthread_start(void) */
ASM_FUNCTION_START(arch_kthread_start, ASM_LINKAGE_GLOBAL):
	movq (%rsp), %rdi /* Function */
	movq 8(%rsp), %rsi /* Argument */

	/* Create a null frame */
	movq $0, (%rsp)
	movq $0, 8(%rsp)
	xorq %rbp, %rbp

	call kthread_start
	ud2
ASM_FUNCTION_END(arch_kthread_start)
