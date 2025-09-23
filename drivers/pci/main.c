#include <lunar/core/module.h>
#include <lunar/core/printk.h>
#include <lunar/core/pci.h>
#include "internal.h"

static const struct pci_hooks legacy_hooks = {
	.open = pci_legacy_device_open,
	.close = pci_legacy_device_close,
	.read = pci_legacy_read,
	.write = pci_legacy_write
};

static const struct pci_hooks mcfg_hooks = {
	.open = pci_mcfg_device_open,
	.close = pci_mcfg_device_close,
	.read = pci_mcfg_read,
	.write = pci_mcfg_write
};

static int pci_module_init(void) {
	int err = pci_mcfg_init();
	if (err) {
		printk("pci: MCFG not found, using legacy\n");
		err = pci_set_hooks(&legacy_hooks);
	} else {
		err = pci_set_hooks(&mcfg_hooks);
	}

	if (err)
		printk(PRINTK_ERR "pci: Failed to set PCI hook, err: %i\n", err);
	return err;
}

MODULE("pci", INIT_STATUS_SCHED, pci_module_init);
