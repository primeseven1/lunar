#include <lunar/core/pci.h>
#include <lunar/core/printk.h>
#include <lunar/core/module.h>

static const struct pci_hooks* pci_hooks = NULL;

int pci_set_hooks(const struct pci_hooks* hooks) {
	if (pci_hooks)
		return -EALREADY;

	pci_hooks = hooks;
	return 0;
}

u8 pci_read_config_byte(struct pci_device* dev, u32 off) {
	if (!pci_hooks)
		return U8_MAX;

	u32 d = pci_hooks->read(dev, off & ~3u);
	return (u8)((d >> ((off & 3u) * 8u)) & 0xFFu);
}

u16 pci_read_config_word(struct pci_device *dev, u32 off) {
	if (off & (sizeof(u16) - 1))
		return U16_MAX;
	if (!pci_hooks)
		return U16_MAX;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;

	u32 d = pci_hooks->read(dev, base);
	return (u16)((d >> (byte * 8u)) & 0xFFFFu);
}

u32 pci_read_config_dword(struct pci_device* dev, u32 off) {
	if (off & (sizeof(u32) - 1))
		return U32_MAX;
	if (!pci_hooks)
		return U32_MAX;

	return pci_hooks->read(dev, off);
}

static inline u32 mask32(unsigned int bits, unsigned int shift) {
	return ((bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u)) << shift);
}

int pci_write_config_byte(struct pci_device* dev, u32 off, u8 val) {
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int shift = (off & 3u) * 8u;
	u32 base = off & ~3u;

	u32 d = pci_hooks->read(dev, base);
	d = (d & ~mask32(8, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, base, d);
}

int pci_write_config_word(struct pci_device* dev, u32 off, u16 val) {
	if (off & (sizeof(u16) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;
	unsigned int shift = byte * 8u;

	u32 d = pci_hooks->read(dev, base);
	d = (d & ~mask32(16, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, base, d);
}

int pci_write_config_dword(struct pci_device* dev, u32 off, u32 val) {
	if (off & (sizeof(u32) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->write(dev, off, val);
}

int pci_device_open(u32 bus, u32 dev, u32 func, struct pci_device** out) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->open(bus, dev, func, out);
}

int pci_device_close(struct pci_device* dev) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->close(dev);
}

static int pci_enumerate_function(u32 bus, u32 dev, u32 func) {
	struct pci_device* device;

	int err = pci_device_open(bus, dev, func, &device);
	if (err)
		return 0;
	u16 vendor = pci_read_config_word(device, PCI_CONFIG_VENDOR);
	pci_device_close(device);

	if (vendor == U16_MAX)
		return 0;

	/* assuming 1 PCI controller (for now) */
	printk("pci: %04x:%02x:%02x.%x found\n", device->domain, device->bus, device->dev, func); /* TODO, do something... */
	return 1;
}

static int pci_enumerate_device(u32 bus, u32 dev) {
	int visited = 0;

	struct pci_device* device;
	int err = pci_device_open(bus, dev, 0, &device);
	if (err)
		return 0;
	if (pci_read_config_word(device, PCI_CONFIG_VENDOR) == U16_MAX)
		return 0;

	u8 header = pci_read_config_byte(device, PCI_CONFIG_HEADER_TYPE);
	bool multi = header & 0x80;
	u32 max_func = multi ? 7 : 0;

	pci_device_close(device);

	for (u32 func = 0; func <= max_func; func++)
		visited += pci_enumerate_function(bus, dev, func);

	return visited;
}

static int pci_enumerate_bus(u32 bus) {
	int visited = 0;
	for (u32 device = 0; device < PCI_MAX_DEV; device++)
		visited += pci_enumerate_device(bus, device);

	return visited;
}

static int pci_enumerate(void) {
	int visited = 0;
	for (u32 bus = 0; bus < PCI_MAX_BUS; bus++)
		visited += pci_enumerate_bus(bus);

	return visited;
}

void pci_init(void) {
	int err = module_load("pci");
	if (unlikely(err == 0 && !pci_hooks))
		printk(PRINTK_ERR "PCI module didn't set PCI hooks!\n");

	printk("pci: Found %d devices\n", pci_enumerate());
}
