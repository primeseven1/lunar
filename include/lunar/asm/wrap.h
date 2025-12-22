#pragma once

#ifndef __ASSEMBLER__

#include <lunar/types.h>

#define cpu_relax() __asm__ volatile("pause" : : : "memory")
#define cpu_halt() __asm__ volatile("hlt" : : : "memory")
#define cpu_ldmxcsr(m) \
	do { \
		u32 __mxcsr = m; \
		__asm__ volatile("ldmxcsr %0" : : "m"(__mxcsr) : "memory"); \
	} while (0)
#define cpu_fxsave(p) __asm__ volatile("fxsave (%0)" : : "r"(p) : "memory")
#define cpu_fxrstor(p) __asm__ volatile("fxrstor (%0)" : : "r"(p) : "memory");

#endif /* __ASSEMBLY_FILE__ */
