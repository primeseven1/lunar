#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/sched.h>
#include <crescent/lib/list.h>

struct cpu {
	struct cpu* self;
	u32 processor_id, lapic_id, sched_processor_id;
	struct mm* mm_struct;
	struct thread* current_thread;
	struct list_head thread_queue;
	spinlock_t thread_lock;
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

void startup_ap_cpus(void);
void bsp_cpu_init(void);
