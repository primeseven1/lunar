#pragma once

#include <uacpi/namespace.h>
#include <uacpi/utilities.h>
#include <lunar/lib/list.h>

uacpi_status acpi_finish_init(void);
uacpi_status acpi_early_init(void);

struct acpi_driver {
	const char* name;
	int (*probe)(uacpi_namespace_node*, uacpi_namespace_node_info*);
	const char* const* pnp_ids;
	struct list_node link;
};

void acpi_driver_register(struct acpi_driver* driver);
void acpi_drivers_load(void);

_Noreturn void acpi_poweroff(void);
_Noreturn void acpi_reboot(void);
