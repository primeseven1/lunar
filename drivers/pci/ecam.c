#include <lunar/mm/heap.h>
#include <lunar/core/printk.h>
#include <lunar/core/io.h>
#include <lunar/core/panic.h>
#include <lunar/lib/string.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include "internal.h"

struct pci_ecam {
	physaddr_t physical;
	void __iomem* virtual;
	size_t len;
	u16 domain;
	u8 start_bus, end_bus;
};

static struct pci_ecam* ecam_list = NULL;
static size_t ecam_count = 0;

static const struct pci_ecam* pci_ecam_find(u16 domain, u8 bus) {
	for (size_t i = 0; i < ecam_count; i++) {
		const struct pci_ecam* e = &ecam_list[i];
		if (e->domain == domain && bus >= e->start_bus && bus <= e->end_bus)
			return e;
	}
	return NULL;
}

static inline size_t pci_ecam_offset(const struct pci_ecam* ecam, const struct pci_device* device, u16 off) {
	size_t b = device->bus - ecam->start_bus;
	return (b << 20) | (device->dev << 15) | (device->func << 12) | (off & 0xFC);
}

#define PCI_MCFG_CONFIG_SIZE 4096u

int pci_ecam_read(struct pci_device* device, u16 off, u32* out) {
	if (off >= PCI_MCFG_CONFIG_SIZE)
		return -EINVAL;

	const struct pci_ecam* e = pci_ecam_find(device->domain, device->bus);
	if (!e)
		return -ENODEV;

	size_t offset = pci_ecam_offset(e, device, off);
	*out = readl((u32 __iomem*)((uintptr_t)e->virtual + offset));
	return 0;
}

int pci_ecam_write(struct pci_device* device, u16 off, u32 value) {
	if (off >= PCI_MCFG_CONFIG_SIZE)
		return -EINVAL;

	const struct pci_ecam* e = pci_ecam_find(device->domain, device->bus);
	if (!e)
		return -ENODEV;

	size_t offset = pci_ecam_offset(e, device, off);
	writel((u32 __iomem*)((uintptr_t)e->virtual + offset), value);
	return 0;
}

int pci_ecam_open(u16 domain, u8 bus, u8 dev, u8 func, struct pci_device** out) {
	*out = NULL;
	if (dev >= PCI_MAX_DEV || func >= PCI_MAX_FUNC)
		return -EINVAL;

	const struct pci_ecam* e = pci_ecam_find(domain, bus);
	if (!e)
		return -ENODEV;
	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->domain = domain;
	device->bus = bus;
	device->dev = dev;
	device->func = func;

	*out = device;
	return 0;
}

int pci_ecam_close(struct pci_device* device) {
	kfree(device);
	return 0;
}

int pci_ecam_init(void) {
	uacpi_table table;
	if (uacpi_table_find_by_signature("MCFG", &table) != UACPI_STATUS_OK)
		return -ENOTSUP;

	int err = 0;

	struct acpi_mcfg* mcfg = table.ptr;
	size_t count = (mcfg->hdr.length - sizeof(*mcfg)) / sizeof(struct acpi_mcfg_allocation);
	if (unlikely(count == 0)) {
		err = -ENOTSUP;
		goto cleanup;
	}

	ecam_list = kzalloc(count * sizeof(*ecam_list), MM_ZONE_NORMAL);
	if (!ecam_list) {
		err = -ENOMEM;
		goto cleanup;
	}

	for (size_t i = 0; i < count; i++) {
		const struct acpi_mcfg_allocation* a = &mcfg->entries[i];

		struct pci_ecam* e = &ecam_list[i];
		e->domain = a->segment;
		e->start_bus = a->start_bus;
		e->end_bus = a->end_bus;

		size_t len = (a->end_bus - a->start_bus + 1) * (1 << 20);
		e->len = len;

		e->physical = a->address;
		e->virtual = iomap(e->physical, len, MMU_READ | MMU_WRITE);
		if (!e->virtual) {
			err = -EIO;
			goto cleanup;
		}
	}

	ecam_count = count;
cleanup:
	if (unlikely(err) && ecam_list) {
		for (size_t i = 0; i < count; i++) {
			struct pci_ecam* e = &ecam_list[i];
			if (e->virtual)
				iounmap(e->virtual, e->len);
		}
		kfree(ecam_list);
	}
	uacpi_table_unref(&table);
	return err;
}
