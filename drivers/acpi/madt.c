#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <crescent/core/io.h>
#include <acpi/madt.h>
#include <crescent/core/printk.h>

static struct acpi_madt* madt;

uacpi_status acpi_madt_init(void) {
	uacpi_table table;
	int err = uacpi_table_find_by_signature("APIC", &table);
	if (err != UACPI_STATUS_OK)
		return err;

	madt = table.ptr;
	return UACPI_STATUS_OK;
}
