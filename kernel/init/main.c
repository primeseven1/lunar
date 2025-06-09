#include <crescent/compiler.h>
#include <crescent/init/status.h>
#include <crescent/core/limine.h>
#include <crescent/core/module.h>
#include <crescent/core/trace.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/cpu.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/apic.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/heap.h>
#include <crescent/asm/segment.h>

static int init_status = INIT_STATUS_NOTHING;
int init_status_get(void) {
	return init_status;
}

_Noreturn __asmlinkage void kernel_main(void); /* Make the compiler happy */
_Noreturn __asmlinkage void kernel_main(void) {
	int base_revision = limine_base_revision();
	if (base_revision != LIMINE_BASE_REVISION)
		goto die;

	bsp_cpu_init();

	/* Attempt to load early modules */
	module_load("liminefb");
	module_load("e9hack");

	int err = tracing_init();
	if (err)
		printk(PRINTK_WARN "init: tracing_init failed with code %i\n", err);

	buddy_init();
	vmm_init();
	segments_init();
	interrupts_init();
	heap_init();

	err = cmdline_parse();
	if (err)
		printk("init: Failed to parse cmdline! err: %i\n", err);

	init_status = INIT_STATUS_BASIC;

	module_load("acpi");

	/* 
	 * At some point, I will add support for the i8259 PIC, 
	 * so that way the kernel doesn't need to panic when ACPI is disabled 
	 */
	err = apic_bsp_init();
	if (err)
		panic("Failed to initialize APIC, err: %i", err);

	const char* loglevel = cmdline_get("loglevel");
	if (loglevel) {
		unsigned int level = *loglevel - '0';
		err = printk_set_level(level);
		if (err == -EINVAL)
			printk(PRINTK_WARN "init: Failed to set cmdline loglevel, invalid level: %u", level);
		else if (err)
			printk(PRINTK_WARN "init: Failed to set cmdline loglevel, err: %i", err);
	}

	printk(PRINTK_CRIT "kernel_main ended!\n");
die:
	while (1)
		__asm__ volatile("hlt");
}
