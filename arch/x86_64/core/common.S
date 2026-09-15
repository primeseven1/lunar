#include <arch/asm/linkage.h>
#include <x86_64/asm/segment.h>

.code64
.text

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
