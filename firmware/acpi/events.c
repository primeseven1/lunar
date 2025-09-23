#include <lunar/core/panic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/printk.h>
#include <lunar/core/interrupt.h>

#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/sleep.h>

#include "events.h"

static void do_poweroff(uacpi_handle ctx) {
	(void)ctx;
	local_irq_disable();

	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_likely(status == UACPI_STATUS_OK)) {
		printk(PRINTK_CRIT "acpi: powering off...\n");
		timekeeper_stall(1000000);
		status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
		if (uacpi_unlikely(status != UACPI_STATUS_OK))
			printk(PRINTK_ERR "acpi: Cannot enter sleep state S5: %s\n", uacpi_status_to_string(status));
	} else {
		printk(PRINTK_ERR "acpi: Cannot prepare for sleep state S5: %s\n", uacpi_status_to_string(status));
	}

	panic("poweroff");
}

uacpi_interrupt_ret acpi_pwrbtn_event(uacpi_handle ctx) {
	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, do_poweroff, ctx);
	while (status != UACPI_STATUS_OK) {
		if (status != UACPI_STATUS_OUT_OF_MEMORY)
			panic("poweroff event failed!\n");
		timekeeper_stall(10);
	}

	return UACPI_INTERRUPT_HANDLED;
}
