#include <lunar/common.h>
#include <lunar/asm/wrap.h>
#include <lunar/core/limine.h>
#include <lunar/core/cpu.h>
#include <lunar/core/panic.h>
#include <lunar/core/printk.h>
#include <lunar/lib/string.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/vmm.h>
#include <lunar/mm/hhdm.h>
#include <lunar/init/head.h>

/* Must be marked as volatile, otherwise checking the response pointer will be optimized away */
static volatile struct limine_mp_request __limine_request mp_request = {
	.request.id = LIMINE_MP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

/* The pointer itself can be modified when another CPU is trying to get it, so access atomically */
static atomic(struct smp_cpus*) smp_cpus;

const struct smp_cpus* smp_cpus_get(void) {
	return atomic_load(&smp_cpus);
}

void cpu_structs_init(void) {
	size_t struct_size = sizeof(struct smp_cpus) + (mp_request.response->cpu_count * sizeof(struct cpu*));
	physaddr_t address = alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(struct_size));
	atomic_store(&smp_cpus, hhdm_virtual(address));
	atomic_load(&smp_cpus)->count = mp_request.response->cpu_count;
}

void cpu_register(void) {
	struct cpu* cpu = current_cpu();
	atomic_load(&smp_cpus)->cpus[cpu->sched_processor_id] = cpu;
}

static atomic(u64) cpus_left;

void cpu_init_finish(void) {
	printk(PRINTK_DBG "smp: CPU %u online\n", current_cpu()->sched_processor_id);
	atomic_sub_fetch(&cpus_left, 1);
}

void cpu_startup_aps(void) {
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

	struct smp_cpus* cpus = atomic_load(&smp_cpus);
	physaddr_t cpu_structs = hhdm_physical(cpus);
	size_t struct_size = sizeof(*cpus) + (cpus->count * sizeof(struct cpu*));

	/* 
	 * Map the structure as read only, any references to the HHDM pointer 
	 * will still work even after this, HHDM just isn't read only 
	 */
	cpus = vmap(NULL, struct_size, MMU_READ, VMM_PHYSICAL, &cpu_structs);
	if (unlikely(IS_PTR_ERR(cpus)))
		printk(PRINTK_WARN "smp: Failed to remap CPU structs as read only\n");
	else
		atomic_store(&smp_cpus, cpus);

	if (cpus->count - 1)
		printk(PRINTK_INFO "smp: Brought up %u CPUs\n", cpus->count - 1);
}

static atomic(u32) sched_ids = atomic_init(1);

void cpu_ap_init(struct limine_mp_info* mp_info) {
	struct cpu* cpu = hhdm_virtual(alloc_pages(MM_ZONE_NORMAL | MM_NOFAIL, get_order(sizeof(*cpu))));
	memset(cpu, 0, sizeof(*cpu));

	cpu->self = cpu;
	cpu->lapic_id = mp_info->lapic_id;
	cpu->processor_id = mp_info->processor_id;
	cpu->sched_processor_id = atomic_fetch_add(&sched_ids, 1);

	wrmsr(MSR_GS_BASE, (uintptr_t)cpu);
}

void cpu_bsp_init(void) {
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
