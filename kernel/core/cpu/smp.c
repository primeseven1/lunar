#include <lunar/asm/wrap.h>
#include <lunar/core/cpu.h>
#include <lunar/core/printk.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/apic.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/init/head.h>
#include "internal.h"

struct limine_mp_request __limine_request g_mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static struct smp_cpus* smp_cpus;
static atomic(u64) cpus_left, stop_cpus_left;
static struct isr* stop_isr = NULL;

static _Noreturn void stop_ipi(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	atomic_sub_fetch(&stop_cpus_left, 1);
	while (1)
		cpu_halt();
}

void smp_cpu_register(void) {
	struct cpu* cpu = current_cpu();
	smp_cpus->cpus[cpu->sched_processor_id] = cpu;
}

const struct smp_cpus* smp_cpus_get(void) {
	return smp_cpus;
}

void smp_send_stop(void) {
	if (!stop_isr)
		return;

	atomic_store(&stop_cpus_left, smp_cpus->count);
	apic_send_ipi(NULL, stop_isr, APIC_IPI_CPU_OTHERS, true);

	struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
	time_t timeout_ns = timespec_to_ns(&ts) + 1000000000; /* 1 second timeout */
	while (atomic_load(&stop_cpus_left)) {
		ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
		if (timespec_to_ns(&ts) >= timeout_ns)
			break;
	}

	if (atomic_load(&stop_cpus_left) == 0)
		return;

	/* Send an NMI */
	apic_send_ipi(NULL, NULL, APIC_IPI_CPU_OTHERS, false);
}

void smp_struct_init(void) {
	size_t size = sizeof(struct smp_cpus) + (g_mp_request.response->cpu_count * sizeof(struct cpu*));
	physaddr_t address = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(size));
	smp_cpus = hhdm_virtual(address);
	smp_cpus->count = g_mp_request.response->cpu_count;
}

void smp_cpu_init_finish(void) {
	printk(PRINTK_DBG "smp: CPU %u online\n", current_cpu()->sched_processor_id);
	atomic_sub_fetch(&cpus_left, 1);
}

void smp_startup(void) {
	if (smp_cpus->count > 1) {
		stop_isr = interrupt_alloc();
		if (unlikely(!stop_isr))
			panic("smp_startup() failed!");
		int err = interrupt_register(stop_isr, stop_ipi, apic_set_irq, -1, NULL, false);
		if (unlikely(err))
			panic("smp_startup() failed: interrupt_register(): %i", err);
	}

	struct limine_mp_response* mp = g_mp_request.response;
	atomic_store(&cpus_left, mp->cpu_count - 1);
	for (u64 i = 0; i < mp->cpu_count; i++) {
		if (mp->cpus[i]->lapic_id == mp->bsp_lapic_id)
			continue;

		/* The CPU's are parked by the bootloader, an atomic write here causes the target CPU to start */
		atomic_store_explicit(&mp->cpus[i]->goto_address, _ap_start, ATOMIC_SEQ_CST);
	}

	while (atomic_load(&cpus_left))
		cpu_relax();

	if (smp_cpus->count - 1)
		printk(PRINTK_INFO "smp: Brought up %u CPUs\n", smp_cpus->count - 1);
}
