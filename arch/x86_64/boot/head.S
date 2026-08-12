#include <arch/asm/linkage.h>
#include <x86_64/asm/msr.h>
#include <x86_64/asm/flags.h>

.code64
.text
ASM_FUNCTION_START(setup_syscall, ASM_LINKAGE_LOCAL):
	/* Enable the syscall/sysret instructions */
	movl $ARCH_X86_64_MSR_EFER, %ecx
	rdmsr
	orl $ARCH_X86_64_MSR_EFER_SCE, %eax
	wrmsr

	/* Tell the CPU what flags should be cleared on syscall entry */
	movl $ARCH_X86_64_MSR_SF_MASK, %ecx
	movl $ARCH_X86_64_RFLAGS_SYSCALL_SF_MASK, %eax
	xorl %edx, %edx
	wrmsr

	/* Tell the CPU where to go to on syscall entry */
	movl $ARCH_X86_64_MSR_LSTAR, %ecx
	leaq arch_x86_64_asm_syscall_entry(%rip), %rdx
	movl %edx, %eax
	shrq $32, %rdx
	wrmsr
	/* No compatibility mode entry */
	movl $ARCH_X86_64_MSR_CSTAR, %ecx
	xorl %eax, %eax
	xorl %edx, %edx
	wrmsr

	ret
ASM_FUNCTION_END(setup_syscall)

ASM_FUNCTION_START(zero_bss, ASM_LINKAGE_LOCAL):
	leaq _ld_kernel_bss_start(%rip), %rdi
	leaq _ld_kernel_bss_end(%rip), %rcx

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
ASM_FUNCTION_END(zero_bss)

ASM_FUNCTION_START(_start, ASM_LINKAGE_GLOBAL):
	cli
	cld

	/* Align the stack and push null frame */
	andq $-16, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	call zero_bss /* The bootloader has probably already done this, do it again anyway */
	call setup_syscall
	call arch_x86_64_kernel_main

	ud2
ASM_FUNCTION_END(_start)

/* void __asmlinkage arch_x86_64_asm_ap_start(struct arch_limine_mp_info* cpu_info) */
ASM_FUNCTION_START(arch_x86_64_asm_ap_start, ASM_LINKAGE_GLOBAL):
	cli
	cld

	/* Align the stack and push null frame */
	andq $-16, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	/*
	 * The CPU info provided by limine is in rdi (a scratch register). before calling setup_syscall(),
	 * save it in rbx (a preserved register) in case rdi gets clobbered
	 */
	movq %rdi, %rbx
	call setup_syscall
	movq %rbx, %rdi
	call arch_x86_64_kernel_ap_main

	ud2
ASM_FUNCTION_END(arch_x86_64_asm_ap_start)
