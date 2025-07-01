#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/sched.h>

struct cpu {
	struct cpu* self;
	u32 processor_id, lapic_id;
	struct vmm_ctx vmm_ctx;
	struct thread* current_thread, *thread_queue;
	spinlock_t thread_queue_lock;
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
