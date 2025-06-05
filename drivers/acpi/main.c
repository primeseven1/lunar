#include <crescent/asm/errno.h>
#include <crescent/core/module.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>
#include <uacpi/context.h>

static int acpi_init(void) {
	/* Let printk handle log levels */
	uacpi_context_set_log_level(UACPI_LOG_DEBUG);
	return 0;
}

MODULE("acpi", INIT_STATUS_BASIC, acpi_init);
