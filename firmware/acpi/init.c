#include <acpi/acpi_init.h>
#include <uacpi/uacpi.h>
#include <uacpi/context.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>

uacpi_status acpi_finish_init(void) {
	uacpi_status status = uacpi_namespace_load();
	if (uacpi_unlikely(status != UACPI_STATUS_OK))
		return status;
	status = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
	if (uacpi_unlikely(status != UACPI_STATUS_OK))
		return status;

	return uacpi_namespace_initialize();
}

uacpi_status acpi_early_init(void) {
	uacpi_context_set_log_level(UACPI_LOG_TRACE);
	uacpi_context_set_proactive_table_checksum(UACPI_TRUE);
	uacpi_initialize(0);
	return 0;
}
