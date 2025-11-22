#include <lunar/asm/segment.h>
#include <lunar/asm/errno.h>

#ifdef CONFIG_KASLR
.data
#else
.section .rodata
#endif /* CONFIG_KASLR */
syscall_table:
syscall_table_end:
.equ SYSCALL_COUNT, (syscall_table_end - syscall_table) / 8

.code64
.text
.globl asm_syscall_entry
.type asm_syscall_entry, @function
asm_syscall_entry:
	cld
	swapgs

	movq %rax, %cr2 /* syscall number, if a page fault happens here we're fucked anyway */
	movq %gs:8, %rax /* current_cpu()->current_thread */
	movq (%rax), %rax /* current_thread()->utk_stack_top */
	xchg %rsp, %rax /* swap to utk_stack_top */

	/* now just save context */
	pushq %rax /* user RSP */
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	movq %cr2, %rax /* restore syscall number */
	pushq %rax
	movq %es, %rdx
	pushq %rdx
	movq %ds, %rdx
	pushq %rdx

	movq $SEGMENT_KERNEL_DATA, %rbx
	movq %rbx, %es
	movq %rbx, %ds

	/* Align stack and push null frame */
	movq %rsp, %rbx
	andq $15, %rbx
	subq %rbx, %rsp
	pushq $0
	pushq $0
	xorq %rbp, %rbp

	movq %r10, %rcx /* RCX is clobbered on syscall, so the fourth argument is in r10 */

	sti
	cmpq SYSCALL_COUNT, %rax
	jae .bad_syscall
	leaq syscall_table(%rip), %r12
	call *(%r12,%rax,8)
	jmp .exit_syscall
.bad_syscall:
	movq $-ENOSYS, %rax
.exit_syscall:
	cli

	/* Remove null stack frame and realign the stack */
	leaq 16(%rsp,%rbx), %rsp

	popq %rdx
	movq %rdx, %ds
	popq %rdx
	movq %rdx, %es
	addq $8, %rsp /* Skip rax, to not overwrite the return value */
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15

	/* restore stack */
	popq %rdi
	xchg %rsp, %rdi

	swapgs
	sysretq
.size asm_syscall_entry, . - asm_syscall_entry
