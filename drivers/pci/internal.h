#pragma once

#include <lunar/types.h>
#include <lunar/asm/errno.h>
#include <lunar/core/pci.h>

int pci_mcfg_init(void);

int pci_mcfg_device_open(u32 bus, u32 dev, u32 func, struct pci_device** out);
int pci_mcfg_device_close(struct pci_device* device);
u32 pci_mcfg_read(struct pci_device* device, u32 off);
int pci_mcfg_write(struct pci_device* device, u32 off, u32 value);

int pci_legacy_device_open(u32 bus, u32 dev, u32 func, struct pci_device** out);
int pci_legacy_device_close(struct pci_device* device);
u32 pci_legacy_read(struct pci_device* device, u32 off);
int pci_legacy_write(struct pci_device* device, u32 off, u32 value);
