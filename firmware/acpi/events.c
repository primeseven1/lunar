#include <lunar/core/panic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/printk.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/cpu.h>
#include <lunar/asm/wrap.h>

#include <acpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/sleep.h>

#include "events.h"

void acpi_poweroff(uacpi_handle ctx) {
	(void)ctx;

	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);

	local_irq_disable();
	smp_send_stop();
	printk_sched_gone();

	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_CRIT "acpi: Cannot prepare for sleep state S5: %s\n", uacpi_status_to_string(status));
		goto die;
	}

	printk(PRINTK_CRIT "acpi: powering off...\n");
	timekeeper_stall(1000000);
	status = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot enter sleep state S5: %s\n", uacpi_status_to_string(status));

die:
	printk(PRINTK_EMERG "acpi: You have to turn your computer off manually.\n");
	while (1)
		cpu_halt();
}

uacpi_interrupt_ret acpi_pwrbtn_event(uacpi_handle ctx) {
	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff, ctx);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_CRIT "acpi: poweroff event failed!\n");

	return UACPI_INTERRUPT_HANDLED;
}
