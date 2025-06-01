#pragma once

#define CTL0_PE (1 << 0)
#define CTL0_MP (1 << 1)
#define CTL0_EM (1 << 2)
#define CTL0_TS (1 << 3)
#define CTL0_ET (1 << 4)
#define CTL0_NE (1 << 5)
#define CTL0_WP (1 << 16)
#define CTL0_NW (1 << 29)
#define CTL0_CD (1 << 30)
#define CTL0_PG (1 << 31)

#define CTL4_VME (1 << 0)
#define CTL4_PVI (1 << 1)
#define CTL4_TSD (1 << 2)
#define CTL4_DE (1 << 3)
#define CTL4_PSE (1 << 4)
#define CTL4_PAE (1 << 5)
#define CTL4_MCE (1 << 6)
#define CTL4_PGE (1 << 7)
#define CTL4_PCE (1 << 8)
#define CTL4_OSFXSR (1 << 9)
#define CTL4_OSXMMEXCEPT (1 << 10)
#define CTL4_UIMP (1 << 11)
#define CTL4_LA57 (1 << 12)
#define CTL4_VMXE (1 << 13)
#define CTL4_SMXE (1 << 14)
#define CTL4_FSGSBASE (1 << 16)
#define CTL4_PCIDE (1 << 17)
#define CTL4_OSXSAVE (1 << 18)
#define CTL4_SMEP (1 << 20)
#define CTL4_SMAP (1 << 21)
#define CTL4_PKE (1 << 22)
#define CTL4_CET (1 << 23)
#define CTL4_PKS (1 << 24)

#ifndef __ASSEMBLER__

#include <crescent/types.h>

static inline unsigned long ctl0_read(void) {
	unsigned long ret;
	__asm__("mov %%cr0, %0" : "=r"(ret));
	return ret;
}

static inline void ctl0_write(unsigned long flags) {
	__asm__ volatile("mov %0, %%cr0" : : "r"(flags) : "memory");
}

static inline void* ctl2_read(void) {
	void* ret;
	__asm__("mov %%cr2, %0" : "=r"(ret) : : "memory");
	return ret;
}

static inline physaddr_t ctl3_read(void) {
	unsigned long ret;
	__asm__("mov %%cr3, %0" : "=r"(ret) : : "memory");
	return (physaddr_t)ret;
}

static inline void ctl3_write(physaddr_t pagetable) {
	__asm__ volatile("mov %0, %%cr3" : : "r"((unsigned long)pagetable) : "memory");
}

static inline unsigned long ctl4_read(void) {
	unsigned long ret;
	__asm__("mov %%cr4, %0" : "=r"(ret));
	return ret;
}

static inline void ctl4_write(unsigned long flags) {
	__asm__ volatile("mov %0, %%cr4" : : "r"(flags) : "memory");
}

#endif /* __ASSEMBLER__ */
