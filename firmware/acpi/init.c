#include <acpi/acpi_init.h>

#include <uacpi/uacpi.h>
#include <uacpi/context.h>
#include <uacpi/tables.h>

#include <crescent/compiler.h>
#include <crescent/mm/heap.h>

int acpi_init(void) {
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

	return 0;
}
