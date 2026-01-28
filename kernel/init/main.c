#include <lunar/compiler.h>
#include <lunar/asm/ctl.h>
#include <lunar/asm/wrap.h>
#include <lunar/asm/segment.h>
#include <lunar/init/status.h>
#include <lunar/core/vfs.h>
#include <lunar/core/limine.h>
#include <lunar/core/module.h>
#include <lunar/core/trace.h>
#include <lunar/core/printk.h>
#include <lunar/core/softirq.h>
#include <lunar/core/panic.h>
#include <lunar/core/pci.h>
#include <lunar/core/cpu.h>
#include <lunar/core/cmdline.h>
#include <lunar/core/term.h>
#include <lunar/core/intctl.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/timekeeper.h>
#include <lunar/input/keyboard.h>
#include <lunar/mm/buddy.h>
#include <lunar/mm/heap.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include <lunar/sched/preempt.h>
#include <lunar/lib/convert.h>
#include <lunar/lib/string.h>
#include <acpi/acpi.h>
#include "internal.h"

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

	long long level;
	int err = kstrtoll(loglevel, 0, &level);
	if (err != 0) {
		printk(PRINTK_ERR "init: Failed to parse integer for cmdline: %i\n", err);
		return;
	}

	err = printk_set_level(level);
	if (err)
		printk(PRINTK_ERR "init: Failed to set loglevel %lld: %i\n", level, err);
}

_Noreturn __asmlinkage void ap_kernel_main(struct limine_mp_info* mp_info) {
	ctl3_write(ctl3_read()); /* Flush TLB */

	percpu_ap_init(mp_info);
	smp_cpu_register();

	vmm_cpu_init();
	segments_init();
	interrupts_cpu_init();
	intctl_init_ap();
	timekeeper_cpu_init();
	sched_cpu_init();
	softirq_cpu_init();

	/* Flush again just in case, even though no memory should be shared at this point */
	ctl3_write(ctl3_read());

	/*
	 * After cpu_init_finish(), INIT_STATUS_SCHED may be set. This allows mutexes and semaphores to work.
	 * Since the BSP is still initializing, the BSP cannot try to block since IRQ's are off on the BSP.
	 *
	 * Disabling preempt with IRQ's enabled allows IPI's like TLB shootdowns to be delivered, but prevents
	 * the CPU from running other threads that may take mutexes or try to wait on semaphores.
	 */
	smp_cpu_init_finish();
	preempt_disable();
	local_irq_enable();
	while (init_status_get() != INIT_STATUS_FINISHED)
		cpu_halt();

	/* One more flush just to be safe */
	ctl3_write(ctl3_read());

	preempt_enable();
	sched_thread_exit();
}

extern const struct filesystem_type _ld_kernel_fstypes_start[];
extern const struct filesystem_type _ld_kernel_fstypes_end[];

static void fs_drivers_load(void) {
	char* modname = NULL;
	size_t modname_size = 0;

	size_t count = ((uintptr_t)_ld_kernel_fstypes_end - (uintptr_t)_ld_kernel_fstypes_start) / sizeof(_ld_kernel_fstypes_start[0]);
	for (size_t i = 0; i < count; i++) {
		const struct filesystem_type* type = &_ld_kernel_fstypes_start[i];

		const char* prefix = "fs-";
		size_t size = strlen(type->name) + __builtin_strlen(prefix) + 1;
		if (size > modname_size) {
			char* tmp = krealloc(modname, size, MM_ZONE_NORMAL);
			if (unlikely(!tmp)) {
				printk(PRINTK_WARN "init: Allocation failed loading fs drivers\n");
				break;
			}
			modname = tmp;
			modname_size = size;
		}

		strcpy(modname, prefix);
		strcat(modname, type->name);

		int err = module_load(modname);
		if (err)
			printk(PRINTK_WARN "init: Failed to load fs %s: %i\n", modname, err);
	}

	if (likely(modname))
		kfree(modname);
}

static inline void print_version(void) {
	const char* compiler;
	int major, minor, patchlevel;
#ifdef CONFIG_LLVM
	compiler = "clang";
	major = __clang_major__;
	minor = __clang_minor__;
	patchlevel = __clang_patchlevel__;
#else
	compiler = "gcc";
	major = __GNUC__;
	minor = __GNUC_MINOR__;
	patchlevel = __GNUC_PATCHLEVEL__;
#endif
	printk("Lunar 0.01 (%s version %d.%d.%d)\n", compiler, major, minor, patchlevel);
}

_Noreturn __asmlinkage void kernel_main(void) {
	int base_revision = limine_base_revision();
	if (base_revision != LIMINE_BASE_REVISION) {
		while (1)
			cpu_halt();
	}

	percpu_bsp_init();

	int err_e9hack = module_load("e9hack"); /* Enable early debugging */
	int err_trace = stack_tracer_init();

	buddy_init();
	smp_struct_init();
	smp_cpu_register();
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

	intctl_init_bsp();
	timekeeper_init();

	pci_init();
	acpi_status = acpi_finish_init();
	if (unlikely(acpi_status != UACPI_STATUS_OK))
		panic("acpi_finish_init(): %s", uacpi_status_to_string(acpi_status));

	sched_init();
	softirq_cpu_init();
	printk_init();
	smp_startup();
	init_status_set(INIT_STATUS_SCHED);

	/* Make sure any changes in PTE's by the AP's are seen by the BSP */
	ctl3_write(ctl3_read());

	acpi_drivers_load();
	vfs_init();
	fs_drivers_load();

	init_status_set(INIT_STATUS_FINISHED);

	log_ram_usage();
	local_irq_enable();

	err = vfs_mount("/", "tmpfs", NULL, NULL);
	if (err)
		panic("Failed to mount root file system");

	initrd_init();
	keyboard_reader_thread_init(); /* Will get removed in the future */

	printk(PRINTK_CRIT "init: kernel_main thread ended!\n");
	print_version();
	sched_thread_exit();
}
