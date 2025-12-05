#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include <lunar/core/syscall.h>
#include <lunar/asm/cpuid.h>
#include <lunar/asm/segment.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/hhdm.h>
#include <lunar/lib/string.h>
#include "internal.h"

static void enable_syscall(void) {
	u32 _unused, edx;
	cpuid(CPUID_EXT_LEAF_PROC_INFO, 0, &_unused, &_unused, &_unused, &edx);
	if (unlikely(!(edx & (1 << 11))))
		panic("syscall instruction unsupported by CPU");

	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | MSR_EFER_SCE);
	wrmsr(MSR_LSTAR, (uintptr_t)asm_syscall_entry);
	wrmsr(MSR_STAR, SEGMENT_KERNEL_CODE << 16 | SEGMENT_USER_CODE);
	wrmsr(MSR_CSTAR, 0);
	wrmsr(MSR_SF_MASK, CPU_FLAG_INTERRUPT);
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

	struct limine_mp_response* mp = g_mp_request.response;
	bsp_cpu.lapic_id = mp->bsp_lapic_id;
	for (u64 i = 0; i < mp->cpu_count; i++) {
		if (mp->cpus[i]->lapic_id == mp->bsp_lapic_id) {
			bsp_cpu.processor_id = mp->cpus[i]->processor_id;
			break;
		}
	}
	wrmsr(MSR_GS_BASE, (uintptr_t)&bsp_cpu);
}
