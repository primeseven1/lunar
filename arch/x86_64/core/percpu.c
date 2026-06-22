#include <lunar/percpu.h>
#include <lunar/limine.h>
#include <lunar/panic.h>

#include <arch/processor.h>
#include <arch/percpu.h>
#include <x86_64/asm/msr.h>

#include "internal.h"

static struct limine_mp_request __limine_request mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.arch_specific_response = NULL,
#ifdef CONFIG_ARCH_X86_64_X2APIC
	.flags = ARCH_X86_64_LIMINE_MP_REQUEST_X2APIC
#else /* CONFIG_ARCH_X86_64_X2APIC */
	.flags = 0
#endif /* CONFIG_ARCH_X86_64_X2APIC */
};

#ifndef CONFIG_SMP

static void halt(struct arch_limine_mp_info* mp_info) {
	(void)mp_info;
	while (1)
		arch_cpu_idle();
}

#else

u32 arch_get_cpu_count(void) {
	return mp_request.arch_specific_response->cpu_count;
}

#endif /* CONFIG_SMP */

void arch_start_cpus(void) {
	struct arch_cpu* acpu_current = &current_cpu()->arch_specific;

	struct arch_limine_mp_response* response = mp_request.arch_specific_response;
	bug(response->cpu_count >= U32_MAX);

	for (u32 i = 0; i < response->cpu_count; i++) {
		struct arch_limine_mp_info* limine_cpuinfo = response->cpus[i];
		if (acpu_current->lapic_id == limine_cpuinfo->lapic_id)
			continue;
#ifdef CONFIG_SMP
		atomic_store(&limine_cpuinfo->goto_address, arch_asm_ap_start);
#else
		atomic_store(&limine_cpuinfo->goto_address, halt);
#endif /* CONFIG_SMP */
	}
}

static inline void set_cpu(struct cpu* cpu) {
	cpu->arch_specific.cpu = cpu;
	arch_x86_64_wrmsr(ARCH_X86_64_MSR_GS_BASE, (uintptr_t)cpu);
}

static struct cpu bsp_cpu;

void arch_x86_64_percpu_ap_init(struct arch_limine_mp_info* cpu_info) {
	struct cpu* cpu = hhdm_virtual(alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(*cpu))));
	memset(cpu, 0, sizeof(*cpu));
	set_cpu(cpu);
	struct arch_cpu* acpu = &cpu->arch_specific;
	acpu->lapic_id = cpu_info->lapic_id;
	acpu->acpi_id = cpu_info->processor_id;
}

void arch_x86_64_percpu_bsp_init(void) {
	set_cpu(&bsp_cpu);
	struct arch_cpu* cpu = &current_cpu()->arch_specific;

	struct arch_limine_mp_response* response = mp_request.arch_specific_response;
	bug(response->cpu_count >= U32_MAX);

	cpu->lapic_id = response->bsp_lapic_id;
	for (u32 i = 0; i < response->cpu_count; i++) {
		struct arch_limine_mp_info* limine_cpuinfo = response->cpus[i];
		if (cpu->lapic_id == limine_cpuinfo->lapic_id) {
			cpu->acpi_id = limine_cpuinfo->processor_id;
			break;
		}
	}
}

struct cpu* arch_current_cpu(void) {
	struct cpu* cpu;
	__asm__("movq %%gs:%c1, %0" : "=r"(cpu) : "i"(offsetof(struct cpu, arch_specific.cpu)));
	return cpu;
}
