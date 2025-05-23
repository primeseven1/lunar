#pragma once

#include <crescent/asm/msr.h>
#include <crescent/mm/vmm.h>

struct cpu {
	struct cpu* self;
	u32 processor_id;
	bool in_interrupt;
	struct vmm_ctx vmm_ctx;
};

/**
 * @brief Get the current CPU's CPU struct
 * @return The address of the CPU struct
 */
static inline struct cpu* current_cpu(void) {
	struct cpu* cpu;
	__asm__("movq %%gs:0, %0" : "=r"(cpu));
	return cpu;
}

void bsp_cpu_init(void);
