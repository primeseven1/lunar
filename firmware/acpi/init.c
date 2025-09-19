#include <crescent/core/cmdline.h>

#include <acpi/acpi_init.h>
#include <uacpi/uacpi.h>
#include <uacpi/context.h>
#include <uacpi/tables.h>
#include <uacpi/utilities.h>
#include <uacpi/event.h>
#include "events.h"

uacpi_status acpi_finish_init(void) {
	uacpi_status status = uacpi_namespace_load();
	if (uacpi_unlikely(status != UACPI_STATUS_OK))
		return status;
	status = uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);
	if (uacpi_unlikely(status != UACPI_STATUS_OK))
		return status;

	status = uacpi_namespace_initialize();
	if (uacpi_unlikely(status != UACPI_STATUS_OK))
		return status;

	uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, acpi_pwrbtn_event, UACPI_NULL);
	return UACPI_STATUS_OK;
}

static uacpi_log_level get_loglevel_from_cmdline(void) {
	const char* acpi_log_level = cmdline_get("acpi.loglevel");
	if (!acpi_log_level)
		return UACPI_LOG_ERROR;

	uacpi_log_level level = *acpi_log_level - '0';
	switch (level) {
	case UACPI_LOG_TRACE:
	case UACPI_LOG_DEBUG:
	case UACPI_LOG_ERROR:
	case UACPI_LOG_WARN:
	case UACPI_LOG_INFO:
		return level;
	}

	return UACPI_LOG_ERROR;
}

uacpi_status acpi_early_init(void) {
	uacpi_context_set_log_level(get_loglevel_from_cmdline());
	uacpi_context_set_proactive_table_checksum(UACPI_TRUE);
	uacpi_initialize(0);
	return 0;
}
