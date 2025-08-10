#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/scheduler.h>
#include <crescent/lib/list.h>
#include <crescent/core/panic.h>

struct cpu {
	struct cpu* self;
	u32 processor_id, lapic_id, sched_processor_id;
	struct mm* mm_struct;
	struct runqueue runqueue;
	bool need_resched;
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

static inline void preempt_disable(void) {
	unsigned long flags = local_irq_save();
	current_cpu()->runqueue.current->preempt_count++;
	local_irq_restore(flags);
}

static inline void preempt_enable(void) {
	unsigned long flags = local_irq_save();
	struct thread* current_thread = current_cpu()->runqueue.current;
	assert(current_thread->preempt_count > 0);
	current_thread->preempt_count--;
	local_irq_restore(flags);
}

void startup_ap_cpus(void);
void bsp_cpu_init(void);
