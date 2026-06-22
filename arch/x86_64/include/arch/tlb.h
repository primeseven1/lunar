#pragma once

#include <lunar/types.h>
#include <arch/page.h>
#include <x86_64/asm/ctl.h>

static inline void arch_tlb_flush_single(uintptr_t virtual) {
	__asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
}

static inline void arch_tlb_flush_all(void) {
	arch_x86_64_ctl3_write(arch_x86_64_ctl3_read()); /* Global pages disabled, this is fine */
}

static inline void arch_tlb_flush_range(uintptr_t virtual, size_t size) {
	size_t count = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (count >= 128) {
		arch_tlb_flush_all();
	} else {
		for (size_t i = 0; i < count; i++)
			arch_tlb_flush_single(virtual + (PAGE_SIZE * i));
	}
}
