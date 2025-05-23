#pragma once

#define MSR_APIC_BASE 0x1B
#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_CSTAR 0xC0000083
#define MSR_SF_MASK 0xC0000084
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

#define MSR_EFER_SCE (1 << 0)
#define MSR_EFER_LME (1 << 8)
#define MSR_EFER_LMA (1 << 10)
#define MSR_EFER_NXE (1 << 11)
#define MSR_EFER_SVME (1 << 12)
#define MSR_EFER_LMSLE (1 << 13)
#define MSR_EFER_FFXSR (1 << 14)
#define MSR_EFER_TCE (1 << 15)

#ifndef __ASSEMBLER__

#include <crescent/types.h>

/**
 * @brief Execute the RDMSR instruction
 * @param msr The MSR to read from
 * @return The value of the MSR
 */
static inline u64 rdmsr(u32 msr) {
	u32 low, high;
	__asm__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
	return ((u64)high << 32) | low;
}

/**
 * @brief Execute the WRMSR instruction
 * @param msr The MSR to write to
 * @param val The value to write to the MSR
 */
static inline void wrmsr(u32 msr, u64 val) {
	u32 low = val & U32_MAX;
	u32 high = val >> 32;
	__asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

#endif /* __ASSEMBLER__ */
