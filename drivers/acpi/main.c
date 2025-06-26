#include <crescent/compiler.h>
#include <crescent/asm/errno.h>
#include <crescent/core/module.h>
#include <crescent/core/printk.h>
#include <crescent/mm/heap.h>
#include <acpi/madt.h>
#include <uacpi/uacpi.h>
#include <uacpi/context.h>

static int acpi_init(void) {
	uacpi_context_set_log_level(UACPI_LOG_DEBUG);
	uacpi_context_set_proactive_table_checksum(UACPI_TRUE);

	void* early_tables = kmalloc(PAGE_SIZE, MM_ZONE_NORMAL);
	if (!early_tables)
		return -ENOMEM;
	uacpi_status err = uacpi_setup_early_table_access(early_tables, PAGE_SIZE);
	if (unlikely(err != UACPI_STATUS_OK)) {
		kfree(early_tables);
		return -ENODATA;
	}

	err = acpi_madt_init();
	if (err != UACPI_STATUS_OK)
		printk("acpi: MADT initialization failed: %i\n", err);

	return 0;
}

MODULE("acpi", INIT_STATUS_MM, acpi_init);
