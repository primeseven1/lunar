#include <arch/asm/linkage.h>
#include <x86_64/asm/segment.h>
#include <x86_64/asm/offsets.h>

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

/* __asmlinkage void context_switch_gp_regs(struct arch_context* current, struct arch_context* next) */
ASM_FUNCTION_START(context_switch_gp_regs, ASM_LINKAGE_GLOBAL):
	/* When switching back to this thread, it will be in kernel mode regardless */
	movq $ARCH_X86_64_SEGMENT_KERNEL_CODE, ARCH_X86_64_CTX_CS_OFFSET(%rdi)
	movq $ARCH_X86_64_SEGMENT_KERNEL_DATA, ARCH_X86_64_CTX_SS_OFFSET(%rdi)
	movq $ARCH_X86_64_SEGMENT_KERNEL_DATA, ARCH_X86_64_CTX_DS_OFFSET(%rdi)
	movq $ARCH_X86_64_SEGMENT_KERNEL_DATA, ARCH_X86_64_CTX_ES_OFFSET(%rdi)

	/* Now save the current thread state */
	movq %rax, ARCH_X86_64_CTX_RAX_OFFSET(%rdi)
	movq %rbx, ARCH_X86_64_CTX_RBX_OFFSET(%rdi)
	movq %rcx, ARCH_X86_64_CTX_RCX_OFFSET(%rdi)
	movq %rdx, ARCH_X86_64_CTX_RDX_OFFSET(%rdi)
	movq %rsi, ARCH_X86_64_CTX_RSI_OFFSET(%rdi)
	movq %rdi, ARCH_X86_64_CTX_RDI_OFFSET(%rdi)
	movq %rbp, ARCH_X86_64_CTX_RBP_OFFSET(%rdi)
	movq %r8,  ARCH_X86_64_CTX_R8_OFFSET(%rdi)
	movq %r9,  ARCH_X86_64_CTX_R9_OFFSET(%rdi)
	movq %r10, ARCH_X86_64_CTX_R10_OFFSET(%rdi)
	movq %r11, ARCH_X86_64_CTX_R11_OFFSET(%rdi)
	movq %r12, ARCH_X86_64_CTX_R12_OFFSET(%rdi)
	movq %r13, ARCH_X86_64_CTX_R13_OFFSET(%rdi)
	movq %r14, ARCH_X86_64_CTX_R14_OFFSET(%rdi)
	movq %r15, ARCH_X86_64_CTX_R15_OFFSET(%rdi)
	movq %rsp, ARCH_X86_64_CTX_RSP_OFFSET(%rdi)
	leaq 2f(%rip), %rax
	movq %rax, ARCH_X86_64_CTX_RIP_OFFSET(%rdi)
	pushfq
	popq %rdx
	movq %rdx, ARCH_X86_64_CTX_RFLAGS_OFFSET(%rdi)

	/* Restore the next thread state */
	movq ARCH_X86_64_CTX_DS_OFFSET(%rsi), %rax
	movq %rax, %ds
	movq ARCH_X86_64_CTX_ES_OFFSET(%rsi), %rax
	movq %rax, %es
	movq ARCH_X86_64_CTX_RAX_OFFSET(%rsi), %rax
	movq ARCH_X86_64_CTX_RBX_OFFSET(%rsi), %rbx
	movq ARCH_X86_64_CTX_RCX_OFFSET(%rsi), %rcx
	movq ARCH_X86_64_CTX_RDX_OFFSET(%rsi), %rdx
	movq ARCH_X86_64_CTX_RDI_OFFSET(%rsi), %rdi
	movq ARCH_X86_64_CTX_RBP_OFFSET(%rsi), %rbp
	movq ARCH_X86_64_CTX_R8_OFFSET(%rsi), %r8
	movq ARCH_X86_64_CTX_R9_OFFSET(%rsi), %r9
	movq ARCH_X86_64_CTX_R10_OFFSET(%rsi), %r10
	movq ARCH_X86_64_CTX_R11_OFFSET(%rsi), %r11
	movq ARCH_X86_64_CTX_R12_OFFSET(%rsi), %r12
	movq ARCH_X86_64_CTX_R13_OFFSET(%rsi), %r13
	movq ARCH_X86_64_CTX_R14_OFFSET(%rsi), %r14
	movq ARCH_X86_64_CTX_R15_OFFSET(%rsi), %r15
	leaq ARCH_X86_64_CTX_RIP_OFFSET(%rsi), %rsp /* Load interrupt frame into rsp */
	movq ARCH_X86_64_CTX_RSI_OFFSET(%rsi), %rsi

	/* If returning to CPL3, swapgs */
	cmpq $ARCH_X86_64_SEGMENT_KERNEL_CODE, 8(%rsp)
	je 1f
	swapgs
1:
	iretq
2:
	ret
ASM_FUNCTION_END(context_switch_gp_regs)
