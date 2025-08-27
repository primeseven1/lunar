#include <crescent/compiler.h>
#include <crescent/asm/wrap.h>
#include <crescent/init/status.h>
#include <crescent/core/limine.h>
#include <crescent/core/module.h>
#include <crescent/core/trace.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/term.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/apic.h>
#include <crescent/core/timekeeper.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/heap.h>
#include <crescent/asm/segment.h>
#include <crescent/sched/scheduler.h>
#include <crescent/sched/kthread.h>

#include <acpi/acpi_init.h>

static int init_status = INIT_STATUS_NOTHING;
int init_status_get(void) {
	return init_status;
}

__diag_push();
__diag_ignore("-Wmissing-prototypes");

_Noreturn __asmlinkage void kernel_main(void) {
	int base_revision = limine_base_revision();
	if (base_revision != LIMINE_BASE_REVISION) {
		while (1)
			__asm__ volatile("hlt");
	}

	bsp_cpu_init();

	int err_e9hack = module_load("e9hack"); /* Enable early debugging */
	int err_trace = tracing_init(); /* Enable stack traces */

	buddy_init();
	cpu_structs_init();
	cpu_register();
	vmm_init();
	segments_init();
	interrupts_init();
	heap_init();

	init_status = INIT_STATUS_MM;

	/* now we can use a generic error variable, since this is the last error before a term is initialized */
	int err = cmdline_parse();

	term_init(); /* Allow the user to actually see errors */
	if (unlikely(err)) {
		printk(PRINTK_ERR "init: Failed to parse cmdline! err: %i\n", err);
	} else {
		const char* loglevel = cmdline_get("loglevel");
		if (loglevel) {
			unsigned int level = *loglevel - '0';
			err = printk_set_level(level);
			if (err == -EINVAL)
				printk(PRINTK_WARN "init: Failed to set cmdline loglevel, invalid level: %u", level);
			else if (err)
				printk(PRINTK_WARN "init: Failed to set cmdline loglevel, err: %i", err);
		}
	}

	if (err_e9hack && err_e9hack != -ENOENT)
		printk(PRINTK_WARN "init: e9hack module init failed (is this real hardware?) err %i\n", err_e9hack);
	if (unlikely(err_trace))
		printk(PRINTK_WARN "init: tracing_init failed with code %i\n", err_trace);

	err = acpi_init();
	if (unlikely(err))
		panic("ACPI failed to initialize, err: %i", err);

	err = apic_bsp_init();
	if (unlikely(err))
		panic("Failed to initialize APIC, err: %i", err);
	timekeeper_init();
	sched_init();
	init_status = INIT_STATUS_SCHED;

	printk(PRINTK_CRIT "init: kernel_main thread ended!\n");
	local_irq_enable();
	sched_thread_exit();
}

__diag_pop();
