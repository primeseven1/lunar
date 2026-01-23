#include <lunar/core/panic.h>
#include <lunar/core/timekeeper.h>
#include <lunar/core/printk.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/cpu.h>
#include <lunar/sched/scheduler.h>
#include <lunar/asm/wrap.h>

#include <acpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/sleep.h>

#include "internal.h"

static void acpi_poweroff_work(uacpi_handle ctx) {
	(void)ctx;
	uacpi_status status = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);

	local_irq_disable();
	smp_send_stop();
	printk_sched_gone();

	if (uacpi_unlikely_error(status))
		goto die;

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

static void acpi_reboot_work(uacpi_handle ctx) {
	(void)ctx;

	local_irq_disable();
	smp_send_stop();
	printk_sched_gone();

	printk(PRINTK_CRIT "acpi: rebooting...\n");
	timekeeper_stall(1000000);
	uacpi_status status = uacpi_reboot();
	if (uacpi_unlikely_error(status))
		printk(PRINTK_ERR "acpi: Cannot reboot: %s\n", uacpi_status_to_string(status));

	printk(PRINTK_EMERG "acpi: You have to reboot manually.\n");
	while (1)
		cpu_halt();
}

static atomic(bool) poweroff_status = atomic_init(false);

_Noreturn void acpi_poweroff(void) {
	while (atomic_exchange(&poweroff_status, true))
		schedule();

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff_work, NULL);
	if (uacpi_likely_success(status))
		uacpi_kernel_wait_for_work_completion();

	panic("poweroff");
}

_Noreturn void acpi_reboot(void) {
	while (atomic_exchange(&poweroff_status, true))
		schedule();

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_reboot_work, NULL);
	if (uacpi_likely_success(status))
		uacpi_kernel_wait_for_work_completion();

	panic("reboot");
}

uacpi_interrupt_ret acpi_pwrbtn_fixed_event(uacpi_handle ctx) {
	if (atomic_exchange(&poweroff_status, true))
		return UACPI_INTERRUPT_HANDLED;

	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_poweroff_work, ctx);
	if (uacpi_unlikely_error(status))
		printk(PRINTK_CRIT "acpi: poweroff event failed!\n");

	return UACPI_INTERRUPT_HANDLED;
}
