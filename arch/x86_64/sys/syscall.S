#include <arch/asm/linkage.h>

.code64
.text

ASM_FUNCTION_START(arch_x86_64_asm_syscall_entry, ASM_LINKAGE_GLOBAL):
	swapgs
	lfence

	swapgs
	lfence
	sysretq
ASM_FUNCTION_END(arch_x86_64_asm_syscall_entry)
