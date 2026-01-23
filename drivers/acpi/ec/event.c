#include <uacpi/event.h>
#include <uacpi/utilities.h>
#include <uacpi/notify.h>
#include <acpi/acpi.h>
#include <lunar/core/printk.h>
#include "ec.h"

#define PWRBTN_PNP_ID "PNP0C0C"

static uacpi_status pwrbtn_notify(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u64 value) {
	(void)ctx;
	(void)node;

	if (value == 0x80)
		acpi_poweroff();
	else
		printk(PRINTK_WARN "ec: Ignoring unkown value %lx from power button device %s\n", value, PWRBTN_PNP_ID);

	return UACPI_STATUS_OK;
}

static uacpi_iteration_decision install_pwrbtn_notify(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
	(void)depth;
	(void)user;
	uacpi_install_notify_handler(node, pwrbtn_notify, NULL);
	return UACPI_ITERATION_DECISION_CONTINUE;
}

void ec_install_events(void) {
	uacpi_find_devices(PWRBTN_PNP_ID, install_pwrbtn_notify, NULL);
}
