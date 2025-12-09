#pragma once

#include <lunar/types.h>
#include <lunar/asm/errno.h>
#include <lunar/core/pci.h>

int pci_ecam_init(void);

int pci_ecam_open(u16 domain, u8 bus, u8 dev, u8 func, struct pci_device** out);
int pci_ecam_close(struct pci_device* device);
int pci_ecam_read(struct pci_device* device, u16 off, u32* out);
int pci_ecam_write(struct pci_device* device, u16 off, u32 value);

int pci_legacy_open(u16 domain, u8 bus, u8 dev, u8 func, struct pci_device** out);
int pci_legacy_close(struct pci_device* device);
int pci_legacy_read(struct pci_device* device, u16 off, u32* out);
int pci_legacy_write(struct pci_device* device, u16 off, u32 value);
