#include <crescent/common.h>
#include <crescent/core/limine.h>
#include <crescent/core/cpu.h>

/* Must be marked as volatile, otherwise checking the response pointer will be optimized away */
static volatile struct limine_mp_request __limine_request mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

void bsp_cpu_init(void) {
	static struct cpu bsp_cpu = {
		.self = &bsp_cpu,
		.processor_id = 0,
		.in_interrupt = false
	};

	struct limine_mp_response* mp = mp_request.response;
	if (mp) {
		for (u64 i = 0; i < mp->cpu_count; i++) {
			if (mp->cpus[i]->lapic_id == mp->bsp_lapic_id) {
				bsp_cpu.processor_id = mp->cpus[i]->processor_id;
				break;
			}
		}
	}

	wrmsr(MSR_GS_BASE, (uintptr_t)&bsp_cpu);
}
