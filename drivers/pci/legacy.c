#include <lunar/core/io.h>
#include <lunar/core/spinlock.h>
#include <lunar/mm/heap.h>
#include "internal.h"

#define PCI_LEGACY_CONFIG_SIZE 256u
#define PCI_LEGACY_CONFIG_PORT 0xCF8
#define PCI_LEGACY_DATA_PORT 0xCFC

static SPINLOCK_DEFINE(lock);

static inline u32 pci_legacy_address(struct pci_device* device, u32 off) {
	return 0x80000000u | (device->bus << 16) | (device->dev << 11) | (device->func << 8) | (off & 0xFC);
}

int pci_legacy_read(struct pci_device* device, u16 off, u32* out) {
	if (off >= PCI_LEGACY_CONFIG_SIZE)
		return -EINVAL;

	irqflags_t irq;
	spinlock_lock_irq_save(&lock, &irq);

	outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, off));
	*out = inl(PCI_LEGACY_DATA_PORT);

	spinlock_unlock_irq_restore(&lock, &irq);
	return 0;
}

int pci_legacy_write(struct pci_device* device, u16 off, u32 value) {
	if (off >= PCI_LEGACY_CONFIG_SIZE)
		return -EINVAL;

	irqflags_t irq;
	spinlock_lock_irq_save(&lock, &irq);

	outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, off));
	outl(PCI_LEGACY_DATA_PORT, value);

	spinlock_unlock_irq_restore(&lock, &irq);
	return 0;
}

int pci_legacy_open(u16 domain, u8 bus, u8 dev, u8 func, struct pci_device** out) {
	*out = NULL;
	if (dev >= PCI_MAX_DEV || func >= PCI_MAX_FUNC)
		return -EINVAL;
	if (domain != 0)
		return -ENODEV;

	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->domain = 0;
	device->bus = bus;
	device->dev = dev;
	device->func = func;

	*out = device;
	return 0;
}

int pci_legacy_close(struct pci_device* device) {
	kfree(device);
	return 0;
}
