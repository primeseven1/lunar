#include <uacpi/event.h>
#include <uacpi/utilities.h>
#include <uacpi/notify.h>
#include <acpi/acpi.h>
#include <lunar/core/printk.h>
#include "ec.h"

static uacpi_status pwrbtn_notify(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u64 value) {
	(void)ctx;
	(void)node;

	/* Make sure poweroff gets scheduled on the BSP */
	if (value == 0x80)
		uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff, NULL);

	return UACPI_STATUS_OK;
}

static uacpi_iteration_decision init_pwrbtn(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
	(void)depth;
	(void)user;
	uacpi_install_notify_handler(node, pwrbtn_notify, NULL);
	return UACPI_ITERATION_DECISION_CONTINUE;
}

void ec_init_events(void) {
	uacpi_find_devices("PNP0C0C", init_pwrbtn, NULL);
}
