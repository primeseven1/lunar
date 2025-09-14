#include <crescent/mm/heap.h>
#include <crescent/core/printk.h>
#include <crescent/core/io.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include "internal.h"

static const struct acpi_mcfg_allocation* mcfg_entries = NULL;
static size_t mcfg_entry_count = 0;

static inline u32 __iomem* pci_address(const struct pci_device* device, u32 func, u32 off) {
	uintptr_t byte_off = ((uintptr_t)func << 12) | (off & 0xFFC);
	return (u32 __iomem*)((uintptr_t)device->virtual + byte_off);
}

static const struct acpi_mcfg_allocation* get_mcfg_entry(u8 bus) {
	const struct acpi_mcfg_allocation* entry;
	size_t i;
	for (i = 0; i < mcfg_entry_count; i++) {
		entry = &mcfg_entries[i];
		if (bus >= entry->start_bus && bus <= entry->end_bus)
			break;
	}

	if (i == mcfg_entry_count)
		return NULL;

	return entry;
}

static inline bool pci_mcfg_args_ok(u32 func, u32 off) {
	return func < PCI_MAX_FUNC && off < 0x1000;
}

u32 pci_mcfg_read(struct pci_device* device, u32 func, u32 off) {
	if (!pci_mcfg_args_ok(func, off))
		return U32_MAX;
	const struct acpi_mcfg_allocation* entry = get_mcfg_entry(device->bus);
	if (!entry)
		return U32_MAX;

	const u32 __iomem* address = pci_address(device, func, off);
	return readl(address);
}

int pci_mcfg_write(struct pci_device* device, u32 func, u32 off, u32 value) {
	if (!pci_mcfg_args_ok(func, off))
		return -EINVAL;
	const struct acpi_mcfg_allocation* entry = get_mcfg_entry(device->bus);
	if (!entry)
		return -ENODEV;

	u32 __iomem* address = pci_address(device, func, off);
	writel(address, value);
	return 0;
}

#define PCI_ECAM_FUNC_SIZE 4096ul
#define PCI_ECAM_DEV_SIZE (PCI_ECAM_FUNC_SIZE * PCI_MAX_FUNC)

static inline size_t ecam_device_offset(const struct acpi_mcfg_allocation* entry, u32 bus, u32 dev) {
	size_t bus_index = bus - entry->start_bus;
	size_t device_index = dev;
	return (bus_index * PCI_MAX_DEV + device_index) * PCI_ECAM_DEV_SIZE;
}

int pci_mcfg_device_open(u32 bus, u32 dev, struct pci_device** out) {
	*out = NULL;
	if (bus >= PCI_MAX_BUS || dev >= PCI_MAX_DEV)
		return -EINVAL;

	const struct acpi_mcfg_allocation* entry = get_mcfg_entry(bus);
	if (!entry)
		return -ENODEV;
	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	physaddr_t device_physical = entry->address + ecam_device_offset(entry, bus, dev);
	device->virtual = iomap(device_physical, PCI_ECAM_DEV_SIZE, MMU_READ | MMU_WRITE);
	if (!device->virtual) {
		kfree(device);
		return -ENOMEM;
	}
	device->bus = bus;
	device->dev = dev;

	u16 vendor = pci_read_config_word(device, 0, PCI_CONFIG_VENDOR);
	if (vendor == 0xFFFF || vendor == 0) {
		bug(iounmap(device->virtual, PCI_ECAM_DEV_SIZE) != 0);
		kfree(device);
		return -ENODEV;
	}

	*out = device;
	return 0;
}

int pci_mcfg_device_close(struct pci_device* device) {
	const struct acpi_mcfg_allocation* entry = get_mcfg_entry(device->bus);
	if (!entry)
		return -EINVAL;
	int err = iounmap(device->virtual, PCI_ECAM_DEV_SIZE);
	if (err)
		return err;

	kfree(device);
	return 0;
}

int pci_mcfg_init(void) {
	uacpi_table table;
	uacpi_status status = uacpi_table_find_by_signature("MCFG", &table);
	if (status != UACPI_STATUS_OK)
		return -ENOSYS;

	struct acpi_mcfg* mcfg = table.ptr;
	mcfg_entries = mcfg->entries;
	mcfg_entry_count = (mcfg->hdr.length - sizeof(*mcfg)) / sizeof(struct acpi_mcfg_allocation);

	return 0;
}
