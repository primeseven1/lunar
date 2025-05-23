#pragma once

/* 
 * There are a lot more CPUID leafs than this, so feel free add more if needed.
 *
 * You may notice that there are CPUID leafs with the same values as other leafs,
 * this is because many CPUID leafs do more than one thing, and I do not want confusing
 * names like CPUID_LEAF_VENDOR_ID_HIGHEST_FUNCTION.
 */

#define CPUID_LEAF_VENDOR_ID 0x00
#define CPUID_LEAF_HIGHEST_FUNCTION 0x00
#define CPUID_LEAF_PROC_INFO 0x01
#define CPUID_LEAF_FEATURE_BITS 0x01

#define CPUID_EXT_LEAF_HIGHEST_FUNCTION 0x80000000
#define CPUID_EXT_LEAF_PROC_INFO 0x80000001
#define CPUID_EXT_LEAF_FEATURE_BITS 0x80000001
#define CPUID_EXT_LEAF_ADDRESS_SIZES 0x80000008

#ifndef __ASSEMBLER__

#include <crescent/types.h>

/**
 * @brief Retrieve CPU information by executing the CPUID instruction
 *
 * If the CPUID leaf/subleaf is not supported by the processor, the behavior is undefined.
 *
 * @param[in] leaf The main CPUID function
 * @param[in] subleaf The subfunction of the main CPUID function
 *
 * @param[out] eax,ebx,ecx,edx Where to put the values of the registers after CPUID is executed
 */
static inline void cpuid(u32 leaf, u32 subleaf,
		u32* eax, u32* ebx, u32* ecx, u32* edx) {
	__asm__("cpuid"
			: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
			: "a"(leaf), "c"(subleaf));
}

#endif /* __ASSEMBLER__ */
