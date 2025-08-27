#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/scheduler.h>
#include <crescent/lib/list.h>
#include <crescent/lib/ringbuffer.h>
#include <crescent/core/panic.h>
#include <crescent/core/semaphore.h>

struct cpu {
	struct cpu* self;
	u32 processor_id, lapic_id, sched_processor_id;
	struct mm* mm_struct;
	struct runqueue runqueue;
	struct ringbuffer deferred_ringbuffer;
	struct semaphore deferred_sem;
	spinlock_t deferred_lock;
	bool need_resched;
};

void cpu_structs_init(void);
struct cpu** get_cpu_structs(u64* count);
void cpu_register(void);

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
