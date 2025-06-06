#include <crescent/asm/errno.h>
#include <crescent/core/module.h>
#include <crescent/mm/heap.h>
#include <acpi/madt.h>
#include <uacpi/uacpi.h>

static int acpi_init(void) {
	void* early_tables = kmalloc(PAGE_SIZE, MM_ZONE_NORMAL);
	if (!early_tables)
		return -ENOMEM;
	uacpi_status err = uacpi_setup_early_table_access(early_tables, PAGE_SIZE);
	if (err != UACPI_STATUS_OK) {
		kfree(early_tables);
		return -ENODATA;
	}

	err = acpi_madt_init();
	if (err != UACPI_STATUS_OK)
		return -ENODATA;

	return 0;
}

MODULE("acpi", INIT_STATUS_BASIC, acpi_init);
