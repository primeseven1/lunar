#pragma once

#include <lunar/limine.h>
#include <x86_64/asm/segment.h>

struct cpu; /* Avoid a circular include */

struct arch_cpu {
	struct cpu* cpu;
	u32 acpi_id, lapic_id;
	struct arch_x86_64_gdt gdt;
	struct arch_x86_64_tss tss;
	struct {
		void* handle;
		u32 ticks_per_1ms;
	} lapic_timer;
};

void arch_x86_64_percpu_ap_init(struct arch_limine_mp_info* cpu_info);
void arch_x86_64_percpu_bsp_init(void);

struct cpu* arch_current_cpu(void);
