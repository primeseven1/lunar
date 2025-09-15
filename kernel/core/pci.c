#include <crescent/core/pci.h>
#include <crescent/core/printk.h>
#include <crescent/core/module.h>

static const struct pci_hooks* pci_hooks = NULL;

int pci_set_hooks(const struct pci_hooks* hooks) {
	if (pci_hooks)
		return -EALREADY;

	pci_hooks = hooks;
	return 0;
}

u8 pci_read_config_byte(struct pci_device* dev, u32 func, u32 off) {
	if (!pci_hooks)
		return U8_MAX;

	u32 d = pci_hooks->read(dev, func, off & ~3u);
	return (u8)((d >> ((off & 3u) * 8u)) & 0xFFu);
}

u16 pci_read_config_word(struct pci_device *dev, u32 func, u32 off) {
	if (!pci_hooks)
		return U16_MAX;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;
	u32 d = pci_hooks->read(dev, func, base);
	return (u16)((d >> (byte * 8u)) & 0xFFFFu);
}

u32 pci_read_config_dword(struct pci_device* dev, u32 func, u32 off) {
	if (!pci_hooks)
		return U32_MAX;

	u32 base = off & ~3u;
	return pci_hooks->read(dev, func, base);
}

static inline u32 mask32(unsigned int bits, unsigned int shift) {
	return ((bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u)) << shift);
}

int pci_write_config_byte(struct pci_device* dev, u32 func, u32 off, u8 val) {
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int shift = (off & 3u) * 8u;
	u32 base = off & ~3u;
	u32 d = pci_hooks->read(dev, func, base);
	d = (d & ~mask32(8, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, func, base, d);
}

int pci_write_config_word(struct pci_device* dev, u32 func, u32 off, u16 val) {
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;
	unsigned int shift = byte * 8u;
	u32 d = pci_hooks->read(dev, func, base);
	d = (d & ~mask32(16, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, func, base, d);
}

int pci_write_config_dword(struct pci_device* dev, u32 func, u32 off, u32 val) {
	if (!pci_hooks)
		return -ENOSYS;

	u32 base = off & ~3u;
	return pci_hooks->write(dev, func, base, val);
}

int pci_device_open(u32 bus, u32 dev, struct pci_device** out) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->open(bus, dev, out);
}

int pci_device_close(struct pci_device* dev) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->close(dev);
}

static int pci_enumerate_function(struct pci_device* dev, u32 func) {
	u16 vendor = pci_read_config_word(dev, func, PCI_CONFIG_VENDOR);
	if (vendor == U16_MAX || vendor == 0)
		return 0;

	/* assuming 1 PCI controller (for now) */
	printk("pci: %04x:%02x:%02x.%x found\n", dev->domain, dev->bus, dev->dev, func); /* TODO, do something... */
	return 1;
}

static int pci_enumerate_device(u32 bus, u32 dev) {
	int visited = 0;

	struct pci_device* device_struct;
	int err = pci_device_open(bus, dev, &device_struct);
	if (err == -ENODEV)
		return 0;
	else if (err)
		return err;

	u8 header = pci_read_config_byte(device_struct, 0, PCI_CONFIG_HEADER_TYPE);
	u32 max_func = (header = U8_MAX) ? 0 : ((header & PCI_HEADER_TYPE_MASK) ? 7 : 0);

	for (u32 func = 0; func <= max_func; func++) {
		int rc = pci_enumerate_function(device_struct, func);
		if (rc < 0) {
			pci_device_close(device_struct);
			return rc;
		}
		visited += rc;
	}

	pci_device_close(device_struct);
	return visited;
}

static int pci_enumerate_bus(u32 bus) {
	int visited = 0;
	for (u32 device = 0; device < PCI_MAX_DEV; device++) {
		int rc = pci_enumerate_device(bus, device);
		if (rc < 0)
			return rc;
		visited += rc;
	}

	return visited;
}

static int pci_enumerate(void) {
	int visited = 0;
	for (u32 bus = 0; bus < PCI_MAX_BUS; bus++) {
		int rc = pci_enumerate_bus(bus);
		if (rc < 0)
			return rc;
		visited += rc;
	}

	return visited;
}

void pci_init(void) {
	int err = module_load("pci");
	if (unlikely(err == 0 && !pci_hooks))
		printk(PRINTK_ERR "PCI module didn't set PCI hooks!\n");

	int rc = pci_enumerate();
	if (rc >= 0)
		printk("pci: Found %d devices\n", rc);
	else
		printk("pci: Failed to enumerate devices: %i\n", rc);
}
