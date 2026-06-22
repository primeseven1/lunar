#include <arch/asm/linkage.h>

/* void __asmlinkage arch_asm_kthread_start(void) */
ASM_FUNCTION_START(arch_asm_kthread_start, ASM_LINKAGE_GLOBAL):
	movq (%rsp), %rdi /* Function */
	movq 8(%rsp), %rsi /* Argument */
	addq $16, %rsp
	
	/* Null frame */
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	call kthread_start
	ud2
ASM_FUNCTION_END(arch_asm_kthread_start)
