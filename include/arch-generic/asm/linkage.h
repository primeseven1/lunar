#pragma once

#ifndef __ASSEMBLER__

#ifndef __asmlinkage
#define __asmlinkage
#endif /* __asmlinkage */

#else /* __ASSEMBLER__ */

#define ASM_LINKAGE_LOCAL(name) /* nothing */
#define ASM_LINKAGE_GLOBAL(name) .globl name

#define ASM_FUNCTION_START(name, linkage) \
	.type name, @function; \
	linkage(name); \
	.align 16; \
	name
#define ASM_FUNCTION_END(name) \
	.size name, . - name

#endif /* __ASSEMBLER__ */
