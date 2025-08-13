#include <crescent/common.h>
#include <crescent/core/limine.h>
#include <crescent/core/cpu.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/vmm.h>
#include "../mm/hhdm.h"

/* Must be marked as volatile, otherwise checking the response pointer will be optimized away */
static volatile struct limine_mp_request __limine_request mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

static struct cpu** cpus = NULL;

void cpu_structs_init(void) {
	size_t size = mp_request.response->cpu_count * sizeof(*cpus);
	physaddr_t address = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(size));
	cpus = hhdm_virtual(address);
}

struct cpu** get_cpu_structs(u64* count) {
	*count = mp_request.response->cpu_count;
	return cpus;
}

void cpu_register(void) {
	struct cpu* cpu = current_cpu();
	cpus[cpu->sched_processor_id] = cpu;
}

void bsp_cpu_init(void) {
	static struct cpu bsp_cpu = {
		.self = &bsp_cpu,
		.sched_processor_id = 0,
	};

	struct limine_mp_response* mp = mp_request.response;
	assert(mp != NULL);

	bsp_cpu.lapic_id = mp->bsp_lapic_id;
	for (u64 i = 0; i < mp->cpu_count; i++) {
		if (mp->cpus[i]->lapic_id == mp->bsp_lapic_id) {
			bsp_cpu.processor_id = mp->cpus[i]->processor_id;
			break;
		}
	}
	wrmsr(MSR_GS_BASE, (uintptr_t)&bsp_cpu);
}
