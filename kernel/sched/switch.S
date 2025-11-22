#include <lunar/asm/segment.h>

.set CTX_DS, 0x00
.set CTX_ES, 0x08
.set CTX_CR2, 0x10
.set CTX_RAX, 0x18
.set CTX_RBX, 0x20
.set CTX_RCX, 0x28
.set CTX_RDX, 0x30
.set CTX_RSI, 0x38
.set CTX_RDI, 0x40
.set CTX_RBP, 0x48
.set CTX_R8, 0x50
.set CTX_R9, 0x58
.set CTX_R10, 0x60
.set CTX_R11, 0x68
.set CTX_R12, 0x70
.set CTX_R13, 0x78
.set CTX_R14, 0x80
.set CTX_R15, 0x88
.set CTX_VECTOR, 0x90
.set CTX_ERR_CODE, 0x98
.set CTX_RIP, 0xa0
.set CTX_CS, 0xa8
.set CTX_RFLAGS, 0xb0
.set CTX_RSP, 0xb8
.set CTX_SS, 0xc0

.code64
.text
.globl asm_context_switch
.type asm_context_switch, @function
asm_context_switch:
	/* rdi = prev, rsi = next */
	movq %cr2, %rax
	movq %rax, CTX_CR2(%rdi)
	movq $SEGMENT_KERNEL_CODE, CTX_CS(%rdi)
	movq $SEGMENT_KERNEL_DATA, CTX_SS(%rdi)
	movq $SEGMENT_KERNEL_DATA, CTX_DS(%rdi)
	movq $SEGMENT_KERNEL_DATA, CTX_ES(%rdi)
	movq %rax, CTX_RAX(%rdi)
	movq %rbx, CTX_RBX(%rdi)
	movq %rcx, CTX_RCX(%rdi)
	movq %rdx, CTX_RDX(%rdi)
	movq %rsi, CTX_RSI(%rdi)
	movq %rdi, CTX_RDI(%rdi)
	movq %rbp, CTX_RBP(%rdi)
	movq %r8,  CTX_R8(%rdi)
	movq %r9,  CTX_R9(%rdi)
	movq %r10, CTX_R10(%rdi)
	movq %r11, CTX_R11(%rdi)
	movq %r12, CTX_R12(%rdi)
	movq %r13, CTX_R13(%rdi)
	movq %r14, CTX_R14(%rdi)
	movq %r15, CTX_R15(%rdi)
	movq %rsp, CTX_RSP(%rdi)
	leaq 1f(%rip), %rax
	movq %rax, CTX_RIP(%rdi)
	pushfq
	popq %rdx
	movq %rdx, CTX_RFLAGS(%rdi)

	/* Set the registers from the next thread */
	movq CTX_DS(%rsi), %rax
	movq %rax, %ds
	movq CTX_ES(%rsi), %rax
	movq %rax, %es
	movq CTX_RAX(%rsi), %rax
	movq CTX_RBX(%rsi), %rbx
	movq CTX_RCX(%rsi), %rcx
	movq CTX_RDX(%rsi), %rdx
	movq CTX_RDI(%rsi), %rdi
	movq CTX_RBP(%rsi), %rbp
	movq CTX_R8(%rsi), %r8
	movq CTX_R9(%rsi), %r9
	movq CTX_R10(%rsi), %r10
	movq CTX_R11(%rsi), %r11
	movq CTX_R12(%rsi), %r12
	movq CTX_R13(%rsi), %r13
	movq CTX_R14(%rsi), %r14
	movq CTX_R15(%rsi), %r15
	leaq CTX_RIP(%rsi), %rsp /* Interrupt frame */
	movq CTX_RSI(%rsi), %rsi
	iretq
1:
	ret
.size asm_context_switch, . - asm_context_switch
