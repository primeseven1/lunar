#pragma once

#include <crescent/asm/msr.h>
#include <crescent/sched/scheduler.h>
#include <crescent/lib/list.h>
#include <crescent/lib/ringbuffer.h>
#include <crescent/core/panic.h>
#include <crescent/core/semaphore.h>
#include <crescent/core/limine.h>

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

struct smp_cpus {
	u32 count;
	struct cpu* cpus[];
};

void cpu_structs_init(void);
const struct smp_cpus* smp_cpus_get(void);
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

void cpu_init_finish(void);
void cpu_startup_aps(void);

void cpu_ap_init(struct limine_mp_info* mp_info);
void cpu_bsp_init(void);
