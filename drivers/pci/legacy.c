#include <crescent/core/io.h>
#include <crescent/mm/heap.h>
#include "internal.h"

#define PCI_LEGACY_CONFIG_SIZE 256u
#define PCI_LEGACY_CONFIG_PORT 0xCF8
#define PCI_LEGACY_DATA_PORT 0xCFC

static inline u32 pci_legacy_address(struct pci_device* device, u32 func, u32 off) {
	return 0x80000000u | (device->bus << 16) | (device->dev << 11) | (func << 8) | (off & 0xFC);
}

static inline bool pci_legacy_args_ok(u32 func, u32 off) {
	return func < PCI_MAX_FUNC && off < PCI_LEGACY_CONFIG_SIZE;
}

u32 pci_legacy_read(struct pci_device* device, u32 func, u32 off) {
	if (!pci_legacy_args_ok(func, off))
		return U32_MAX;

	outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, func, off));
	return inl(PCI_LEGACY_DATA_PORT);
}

int pci_legacy_write(struct pci_device* device, u32 func, u32 off, u32 value) {
	if (!pci_legacy_args_ok(func, off))
		return -EINVAL;

	outl(PCI_LEGACY_CONFIG_PORT, pci_legacy_address(device, func, off));
	outl(PCI_LEGACY_DATA_PORT, value);
	return 0;
}

int pci_legacy_device_open(u32 bus, u32 dev, struct pci_device** out) {
	*out = NULL;
	if (bus >= PCI_MAX_BUS || dev >= PCI_MAX_DEV)
		return -EINVAL;

	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->bus = bus;
	device->dev = dev;
	device->virtual = NULL;

	u16 vendor = pci_read_config_word(device, 0, PCI_CONFIG_VENDOR);
	if (vendor == U16_MAX || vendor == 0) {
		kfree(device);
		return -ENODEV;
	}

	*out = device;
	return 0;
}

int pci_legacy_device_close(struct pci_device* device) {
	kfree(device);
	return 0;
}
