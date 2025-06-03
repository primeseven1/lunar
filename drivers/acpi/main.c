#include <crescent/asm/errno.h>
#include <crescent/core/module.h>
#include <uacpi/status.h>
#include <uacpi/tables.h>

static int acpi_init(void) {
	return 0;
}

MODULE("acpi", INIT_STATUS_BASIC, acpi_init);
