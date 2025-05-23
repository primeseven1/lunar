#include <crescent/compiler.h>
#include <crescent/core/limine.h>
#include <crescent/core/module.h>
#include <crescent/core/trace.h>
#include <crescent/core/printk.h>
#include <crescent/core/cpu.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/interrupt.h>
#include <crescent/mm/buddy.h>
#include <crescent/mm/heap.h>
#include <crescent/asm/segment.h>

_Noreturn __asmlinkage void kernel_main(void); /* Make the compiler happy */
_Noreturn __asmlinkage void kernel_main(void) {
	int base_revision = limine_base_revision();
	if (base_revision != LIMINE_BASE_REVISION)
		goto die;

	bsp_cpu_init();

	module_load("liminefb");

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

	printk(PRINTK_CRIT "kernel_main ended!\n");
die:
	while (1)
		__asm__ volatile("hlt");
}
