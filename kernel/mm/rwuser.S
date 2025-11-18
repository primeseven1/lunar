#include <lunar/asm/errno.h>

.code64
.text
.globl asm_user_read_u8
.type asm_user_read_u8, @function
asm_user_read_u8:
	movzbl (%rdi), %eax
	ret
.size asm_user_read_u8, . - asm_user_read_u8

.globl asm_user_write_u8
.type asm_user_write_u8, @function
asm_user_write_u8:
	movb %sil, (%rdi)
	xorl %eax, %eax
	ret
.size asm_user_write_u8, . - asm_user_write_u8

.globl asm_user_fixup
.type asm_user_fixup, @function
asm_user_fixup:
	movl $-EFAULT, %eax
	ret
.size asm_user_fixup, . - asm_user_fixup
