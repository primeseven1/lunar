#include <lunar/mm/heap.h>
#include <lunar/core/printk.h>
#include <lunar/core/io.h>
#include <lunar/core/panic.h>
#include <lunar/lib/string.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#include "internal.h"

struct ecam_seg {
	physaddr_t physical;
	size_t len;
	void __iomem* virtual;
	u32 domain;
	u8 start_bus, end_bus;
};

#define PCI_MCFG_CONFIG_SIZE 4096u

static struct ecam_seg* ecam_segs;
static size_t ecam_seg_count;

static inline u32 __iomem* pci_address(const struct pci_device* device, u32 off) {
	uintptr_t byte_off = ((uintptr_t)device->func << 12) | (off & 0xFFC);
	return (u32 __iomem*)((uintptr_t)device->virtual + byte_off);
}

static const struct ecam_seg* get_ecam_entry(u8 bus) {
	const struct ecam_seg* entry;
	size_t i;
	for (i = 0; i < ecam_seg_count; i++) {
		entry = &ecam_segs[i];
		if (bus >= entry->start_bus && bus <= entry->end_bus)
			break;
	}

	if (i == ecam_seg_count)
		return NULL;

	return entry;
}

static inline bool pci_mcfg_args_ok(u32 off) {
	return off < PCI_MCFG_CONFIG_SIZE;
}

u32 pci_mcfg_read(struct pci_device* device, u32 off) {
	if (!pci_mcfg_args_ok(off))
		return U32_MAX;
	const struct ecam_seg* entry = get_ecam_entry(device->bus);
	if (!entry)
		return U32_MAX;

	const u32 __iomem* address = pci_address(device, off);
	return readl(address);
}

int pci_mcfg_write(struct pci_device* device, u32 off, u32 value) {
	if (!pci_mcfg_args_ok(off))
		return -EINVAL;
	const struct ecam_seg* entry = get_ecam_entry(device->bus);
	if (!entry)
		return -ENODEV;

	u32 __iomem* address = pci_address(device, off);
	writel(address, value);
	return 0;
}

#define PCI_ECAM_FUNC_SIZE 4096ul
#define PCI_ECAM_DEV_SIZE  (PCI_ECAM_FUNC_SIZE * PCI_MAX_FUNC)
#define PCI_ECAM_BUS_SIZE (PCI_ECAM_FUNC_SIZE * PCI_MAX_FUNC * PCI_MAX_DEV)

static inline size_t ecam_device_offset(const struct ecam_seg* entry, u32 bus, u32 dev) {
	size_t bus_index = bus - entry->start_bus;
	return (bus_index * PCI_MAX_DEV + dev) * PCI_ECAM_DEV_SIZE;
}

int pci_mcfg_device_open(u32 bus, u32 dev, u32 func, struct pci_device** out) {
	*out = NULL;
	if (bus >= PCI_MAX_BUS || dev >= PCI_MAX_DEV)
		return -EINVAL;

	const struct ecam_seg* entry = get_ecam_entry(bus);
	if (!entry)
		return -ENODEV;

	struct pci_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	uintptr_t off = ecam_device_offset(entry, bus, dev);
	device->virtual = (void __iomem*)((uintptr_t)entry->virtual + off);
	device->domain = entry->domain;
	device->bus = bus;
	device->dev = dev;
	device->func = func;

	*out = device;
	return 0;
}

int pci_mcfg_device_close(struct pci_device* device) {
	kfree(device);
	return 0;
}

int pci_mcfg_init(void) {
	uacpi_table table;
	uacpi_status status = uacpi_table_find_by_signature("MCFG", &table);
	if (status != UACPI_STATUS_OK)
		return -ENOSYS;

	struct acpi_mcfg* mcfg = table.ptr;
	const struct acpi_mcfg_allocation* mcfg_entries = mcfg->entries;
	size_t mcfg_entry_count = (mcfg->hdr.length - sizeof(*mcfg)) / sizeof(struct acpi_mcfg_allocation);

	int err = 0;
	if (unlikely(mcfg_entry_count == 0)) {
		err = -ENOSYS; /* ? */
		goto cleanup;
	}

	ecam_segs = kzalloc(sizeof(*ecam_segs) * mcfg_entry_count, MM_ZONE_NORMAL);
	if (!ecam_segs) {
		err = -ENOMEM;
		goto cleanup;
	}

	for (ecam_seg_count = 0; ecam_seg_count < mcfg_entry_count; ecam_seg_count++) {
		physaddr_t physical = mcfg_entries[ecam_seg_count].address;
		size_t len = (mcfg_entries[ecam_seg_count].end_bus - mcfg_entries[ecam_seg_count].start_bus + 1) * PCI_ECAM_BUS_SIZE;
		void __iomem* virtual = iomap(physical, len, MMU_READ | MMU_WRITE);
		if (!virtual) {
			err = -EIO;
			goto cleanup;
		}

		ecam_segs[ecam_seg_count].domain = ecam_seg_count;
		ecam_segs[ecam_seg_count].physical = physical;
		ecam_segs[ecam_seg_count].virtual = virtual;
		ecam_segs[ecam_seg_count].len = len;
		ecam_segs[ecam_seg_count].start_bus = mcfg_entries[ecam_seg_count].start_bus;
		ecam_segs[ecam_seg_count].end_bus = mcfg_entries[ecam_seg_count].end_bus;
	}

cleanup:
	uacpi_table_unref(&table);
	if (err && ecam_segs) {
		for (size_t i = 0; i < ecam_seg_count; i++)
			iounmap(ecam_segs[i].virtual, ecam_segs[i].len);
		kfree(ecam_segs);
	}
	return err;
}
