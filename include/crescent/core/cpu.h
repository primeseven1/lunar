#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/sched.h>

struct cpu {
	struct cpu* self;
	u32 processor_id, lapic_id, sched_processor_id;
	struct mm* mm_struct;
	thread_t* current_thread, *thread_queue;
	atomic(unsigned long) thread_count;
	spinlock_t thread_queue_lock;
};

struct cpu** get_cpu_structs(u64* count);

/**
 * @brief Get the current CPU's CPU struct
 * @return The address of the CPU struct
 */
static inline struct cpu* current_cpu(void) {
	struct cpu* cpu;
	__asm__("movq %%gs:0, %0" : "=r"(cpu));
	return cpu;
}

void cpu_startup_aps(void);
void bsp_cpu_init(void);
