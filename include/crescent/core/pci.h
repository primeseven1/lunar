#pragma once

#include <crescent/types.h>
#include <crescent/asm/errno.h>
#include <crescent/compiler.h>

#define PCI_MAX_BUS 256u
#define PCI_MAX_FUNC 8u
#define PCI_MAX_DEV 32u

enum pci_configs {
	PCI_CONFIG_VENDOR,
	PCI_CONFIG_DEVICE_ID = 0x02,
	PCI_CONFIG_COMMAND = 0x04,
	PCI_CONFIG_STATUS = 0x06,

	PCI_CONFIG_REVISION = 0x08,
	PCI_CONFIG_PROGIF = 0x09,
	PCI_CONFIG_SUBCLASS = 0x0a,
	PCI_CONFIG_CLASS = 0x0b,
	PCI_CONFIG_HEADER_TYPE = 0x0e,
	PCI_CONFIG_CAP = 0x34
};

enum pci_commands {
	PCI_COMMAND_IO = (1 << 0),
	PCI_COMMAND_MMIO = (1 << 1),
	PCI_COMMAND_BUSMASTER = (1 << 2),
	PCI_COMMAND_IRQ_DISABLE = (1 << 10)
};

enum pci_statuses {
	PCI_STATUS_HAS_CAP = 0x10
};

enum pci_subclasses {
	PCI_SUBCLASS_STORAGE_NVM = 0x08
};

enum pci_classes {
	PCI_CLASS_STORAGE = 0x01
};

enum pci_header_types {
	PCI_HEADER_TYPE_MULTIFUNCTION_MASK = 0x80,
	PCI_HEADER_TYPE_MASK = 0x7f,
	PCI_HEADER_TYPE_STANDARD = 0
};

enum pci_caps {
	PCI_CAP_MSI = 0x05,
	PCI_CAP_VENDOR_SPECIFIC = 0x09,
	PCI_CAP_MSIX = 0x11
};

struct pci_device {
	u32 domain, bus, dev;
	void __iomem* virtual;
};

struct pci_hooks {
	int (*open)(u32, u32, struct pci_device**);
	int (*close)(struct pci_device*);
	u32 (*read)(struct pci_device*, u32, u32);
	int (*write)(struct pci_device*, u32, u32, u32);
};

u8 pci_read_config_byte(struct pci_device* dev, u32 func, u32 off);
u16 pci_read_config_word(struct pci_device *dev, u32 func, u32 off);
u32 pci_read_config_dword(struct pci_device* dev, u32 func, u32 off);
int pci_write_config_byte(struct pci_device* dev, u32 func, u32 off, u8 val);
int pci_write_config_word(struct pci_device* dev, u32 func, u32 off, u16 val);
int pci_write_config_dword(struct pci_device* dev, u32 func, u32 off, u32 val);

int pci_device_open(u32 bus, u32 dev, struct pci_device** out);
int pci_device_close(struct pci_device* dev);

int pci_set_hooks(const struct pci_hooks* hooks);
void pci_init(void);
