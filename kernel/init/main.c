#include <lunar/compiler.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/wrap.h>
#include <lunar/asm/segment.h>
#include <lunar/init/status.h>
#include <lunar/core/limine.h>
#include <lunar/core/module.h>
#include <lunar/core/trace.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/pci.h>
#include <lunar/core/cpu.h>
#include <lunar/core/cmdline.h>
#include <lunar/core/term.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/apic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/heap.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include <lunar/lib/convert.h>

#include <acpi/acpi_init.h>

static atomic(int) init_status = atomic_init(INIT_STATUS_NOTHING);
int init_status_get(void) {
	return atomic_load(&init_status);
}
static inline void init_status_set(int status) {
	atomic_store(&init_status, status);
}

static inline void log_ram_usage(void) {
	u64 total;
	u64 mem = get_free_memory(&total);
	printk("RAM usage: %ld KiB / %ld KiB\n", mem >> 10, total >> 10);
}

static void set_loglevel(void) {
	const char* loglevel = cmdline_get("loglevel");
	if (!loglevel)
		return;

	unsigned long long level;
	int err = kstrtoull(loglevel, 0, &level);
	if (err != 0) {
		printk(PRINTK_ERR "init: Failed to parse integer for cmdline: %i\n", err);
		return;
	}

	err = printk_set_level(level);
	if (err)
		printk(PRINTK_ERR "init: Failed to set loglevel %llu: %i\n", level, err);
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

_Noreturn __asmlinkage void ap_kernel_main(struct limine_mp_info* mp_info) {
	ctl3_write(ctl3_read()); /* Flush TLB */

	cpu_ap_init(mp_info);
	cpu_register();

	vmm_cpu_init();
	segments_init();
	interrupts_cpu_init();
	apic_ap_init();
	timekeeper_cpu_init();
	sched_cpu_init();

	ctl3_write(ctl3_read()); /* Flush again */
	cpu_init_finish();

	local_irq_enable();
	sched_thread_exit();
}

_Noreturn __asmlinkage void kernel_main(void) {
	int base_revision = limine_base_revision();
	if (base_revision != LIMINE_BASE_REVISION) {
		while (1)
			cpu_halt();
	}

	cpu_bsp_init();

	int err_e9hack = module_load("e9hack"); /* Enable early debugging */
	int err_trace = tracing_init(); /* Enable stack traces */

	buddy_init();
	cpu_structs_init();
	cpu_register();
	vmm_init();
	segments_init();
	interrupts_init();
	vmm_tlb_init();
	heap_init();

	init_status_set(INIT_STATUS_MM);

	/* now we can use a generic error variable, since this is the last error before a term is initialized */
	int err = cmdline_parse();

	term_init(); /* Allow the user to actually see errors */
	if (unlikely(err))
		printk(PRINTK_ERR "init: Failed to parse cmdline! err: %i\n", err);
	else
		set_loglevel();

	if (err_e9hack && err_e9hack != -ENOENT)
		printk(PRINTK_WARN "init: e9hack module init failed (is this real hardware?) err %i\n", err_e9hack);
	if (unlikely(err_trace))
		printk(PRINTK_WARN "init: tracing_init failed with code %i\n", err_trace);

	uacpi_status acpi_status = acpi_early_init();
	if (unlikely(acpi_status != UACPI_STATUS_OK))
		panic("acpi_early_init(): %s", uacpi_status_to_string(acpi_status));

	err = apic_bsp_init();
	if (unlikely(err))
		panic("Failed to initialize APIC, err: %i", err);
	timekeeper_init();
	sched_init();
	cpu_startup_aps();
	init_status_set(INIT_STATUS_SCHED);

	sched_change_prio(current_thread(), SCHED_PRIO_MAX);

	pci_init();
	acpi_status = acpi_finish_init();
	if (unlikely(acpi_status != UACPI_STATUS_OK))
		panic("acpi_finish_init(): %s", uacpi_status_to_string(acpi_status));

	log_ram_usage();
	printk(PRINTK_CRIT "init: kernel_main thread ended!\n");

	local_irq_enable();
	sched_thread_exit();
}

__diag_pop();
