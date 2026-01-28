#include <lunar/asm/wrap.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/segment.h>
#include <lunar/core/cpu.h>
#include <lunar/core/printk.h>
#include <lunar/core/syscall.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/intctl.h>
#include <lunar/lib/string.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/init/head.h>

static struct limine_mp_request __limine_request mp_request = {
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

	atomic_store(&stop_cpus_left, smp_cpus->count - 1);
	for (u32 i = 0; i < smp_cpus->count; i++) {
		if (likely(smp_cpus->cpus[i] != current_cpu()))
			intctl_send_ipi(smp_cpus->cpus[i], stop_isr, 0);
	}
	time_t timeout_ns = timespec_ns(timekeeper_time(TIMEKEEPER_FROMBOOT)) + 1000000000; /* 1 second timeout */
	while (atomic_load(&stop_cpus_left)) {
		if (timespec_ns(timekeeper_time(TIMEKEEPER_FROMBOOT)) >= timeout_ns)
			break;
	}

	if (atomic_load(&stop_cpus_left) == 0)
		return;
	for (u32 i = 0; i < smp_cpus->count; i++) {
		if (likely(smp_cpus->cpus[i] != current_cpu()))
			intctl_send_ipi(smp_cpus->cpus[i], stop_isr, INTCTL_IPI_CRITICAL);
	}
}

void smp_struct_init(void) {
	size_t size = sizeof(struct smp_cpus) + (mp_request.response->cpu_count * sizeof(struct cpu*));
	physaddr_t address = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(size));
	smp_cpus = hhdm_virtual(address);
	smp_cpus->count = mp_request.response->cpu_count;
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
		int err = interrupt_register(stop_isr, NULL, stop_ipi);
		if (unlikely(err))
			panic("smp_startup() failed: interrupt_register(): %i", err);
	}

	struct limine_mp_response* mp = mp_request.response;
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

static void enable_syscall(void) {
	u32 _unused, edx;
	cpuid(CPUID_EXT_LEAF_PROC_INFO, 0, &_unused, &_unused, &_unused, &edx);
	if (unlikely(!(edx & (1 << 11))))
		panic("syscall instruction unsupported by CPU\n");

	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | MSR_EFER_SCE);
	wrmsr(MSR_LSTAR, (uintptr_t)asm_syscall_entry);
	wrmsr(MSR_STAR, SEGMENT_KERNEL_CODE << 16 | SEGMENT_USER_CODE);
	wrmsr(MSR_CSTAR, 0);
	wrmsr(MSR_SF_MASK, CPU_FLAG_INTERRUPT);
}

static void enable_sse(void) {
	u32 edx, _unused;

	/* Check for fxsave/fxrstor, any real x86_64 bit cpu will have this bit set */
	cpuid(1, 0, &_unused, &_unused, &_unused, &edx);
	if (unlikely(!(edx & (1 << 24))))
		panic("CPU does not support fxsave/fxrstor\n");

	unsigned long ctl = ctl0_read();
	ctl &= ~CTL0_EM;
	ctl |= CTL0_MP;
	ctl0_write(ctl);
	ctl = ctl4_read();
	ctl |= CTL4_OSFXSR | CTL4_OSXMMEXCEPT;
	ctl4_write(ctl);

	cpu_ldmxcsr(0x1f80);
}

static atomic(u32) sched_ids = atomic_init(1);

void percpu_ap_init(struct limine_mp_info* mp_info) {
	struct cpu* cpu = hhdm_virtual(alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(*cpu))));
	memset(cpu, 0, sizeof(*cpu));

	cpu->self = cpu;
	cpu->lapic_id = mp_info->lapic_id;
	cpu->processor_id = mp_info->processor_id;
	cpu->sched_processor_id = atomic_fetch_add(&sched_ids, 1);
	wrmsr(MSR_GS_BASE, (uintptr_t)cpu);

	enable_syscall();
}

void percpu_bsp_init(void) {
	static struct cpu bsp_cpu = {
		.self = &bsp_cpu,
		.sched_processor_id = 0,
	};

	struct limine_mp_response* mp = mp_request.response;
	bsp_cpu.lapic_id = mp->bsp_lapic_id;
	for (u64 i = 0; i < mp->cpu_count; i++) {
		if (mp->cpus[i]->lapic_id == mp->bsp_lapic_id) {
			bsp_cpu.processor_id = mp->cpus[i]->processor_id;
			break;
		}
	}
	wrmsr(MSR_GS_BASE, (uintptr_t)&bsp_cpu);

	enable_syscall();
	enable_sse();
}
