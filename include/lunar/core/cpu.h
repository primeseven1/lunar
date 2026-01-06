#pragma once

#include <lunar/asm/msr.h>
#include <lunar/asm/segment.h>
#include <lunar/sched/scheduler.h>
#include <lunar/lib/list.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/panic.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/limine.h>

struct cpu {
	struct cpu* self;
	struct thread* current_thread; /* Alias for runqueue.current */
	u32 processor_id, lapic_id, sched_processor_id;
	struct mm* mm_struct;
	struct runqueue runqueue;
	struct list_head workqueue;
	struct semaphore workqueue_sem;
	spinlock_t workqueue_lock;
	bool need_resched;
	struct timekeeper_source* timekeeper;
	unsigned long softirqs_pending;
	struct tss_descriptor* tss;
};

/* Sanity check, assembly code will expect this offset */
static_assert(offsetof(struct cpu, current_thread) == 8, "offsetof(struct cpu, current_thread) == 8");

struct smp_cpus {
	u32 count;
	struct cpu* cpus[];
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

void percpu_bsp_init(void);
void percpu_ap_init(struct limine_mp_info* mp_info);

void smp_cpu_register(void);
const struct smp_cpus* smp_cpus_get(void);
void smp_send_stop(void);
void smp_struct_init(void);
void smp_cpu_init_finish(void);
void smp_startup(void);
