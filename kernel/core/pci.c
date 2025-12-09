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

int pci_read_config_byte(struct pci_device* dev, u16 off, u8* out) {
	if (!pci_hooks)
		return -ENOSYS;

	u32 _out;
	int err = pci_hooks->read(dev, off & ~3u, &_out);
	if (err)
		return err;

	*out = (u8)((_out >> ((off & 3u) * 8u)) & 0xFFu);
	return 0;
}

int pci_read_config_word(struct pci_device* dev, u16 off, u16* out) {
	if (off & (sizeof(u16) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;

	u32 _out;
	int err = pci_hooks->read(dev, base, &_out);
	if (err)
		return err;

	*out = (u16)((_out >> (byte * 8u)) & 0xFFFFu);
	return 0;
}

int pci_read_config_dword(struct pci_device* dev, u16 off, u32* out) {
	if (off & (sizeof(u32) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;

	return pci_hooks->read(dev, off, out);
}

static inline u32 mask32(unsigned int bits, unsigned int shift) {
	return ((bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u)) << shift);
}

int pci_write_config_byte(struct pci_device* dev, u16 off, u8 val) {
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int shift = (off & 3u) * 8u;
	u32 base = off & ~3u;

	u32 _val;
	int err = pci_hooks->read(dev, base, &_val);
	if (err)
		return err;

	_val = (_val & ~mask32(8, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, base, _val);
}

int pci_write_config_word(struct pci_device* dev, u16 off, u16 val) {
	if (off & (sizeof(u16) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;

	unsigned int byte = off & 3u;
	u32 base = off & ~3u;
	unsigned int shift = byte * 8u;

	u32 _val;
	int err = pci_hooks->read(dev, base, &_val);
	if (err)
		return err;

	_val = (_val & ~mask32(16, shift)) | ((u32)val << shift);
	return pci_hooks->write(dev, base, _val);
}

int pci_write_config_dword(struct pci_device* dev, u16 off, u32 val) {
	if (off & (sizeof(u32) - 1))
		return -EINVAL;
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->write(dev, off, val);
}

int pci_device_open(u16 domain, u8 bus, u8 dev, u8 func, struct pci_device** out) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->open(domain, bus, dev, func, out);
}

int pci_device_close(struct pci_device* dev) {
	if (!pci_hooks)
		return -ENOSYS;
	return pci_hooks->close(dev);
}

static int pci_enumerate_function(u16 domain, u8 bus, u8 dev, u8 func) {
	struct pci_device* device;

	int err = pci_device_open(domain, bus, dev, func, &device);
	if (err)
		return 0;
	u16 vendor;
	err = pci_read_config_word(device, PCI_CONFIG_VENDOR, &vendor);
	pci_device_close(device);

	if (err || vendor == U16_MAX)
		return 0;

	/* assuming 1 PCI controller (for now) */
	printk("pci: %04x:%02x:%02x.%x found\n", domain, bus, dev, func); /* TODO, do something... */
	return 1;
}

static int pci_enumerate_device(u16 domain, u8 bus, u8 dev) {
	int visited = 0;

	struct pci_device* device;
	int err = pci_device_open(domain, bus, dev, 0, &device);
	if (err)
		return err == -ENODEV ? -ENODEV : 0;

	u16 vendor;
	err = pci_read_config_word(device, PCI_CONFIG_VENDOR, &vendor);
	if (err || vendor == U16_MAX)
		goto out;
	u8 header;
	err = pci_read_config_byte(device, PCI_CONFIG_HEADER_TYPE, &header);
	if (err)
		goto out;
	bool multi = header & 0x80;
	u32 max_func = multi ? 7 : 0;

	for (u32 func = 0; func <= max_func; func++)
		visited += pci_enumerate_function(domain, bus, dev, func);
out:
	pci_device_close(device);
	return visited;
}

static int pci_enumerate_bus(u16 domain, u8 bus) {
	int visited = 0;
	for (u32 device = 0; device < PCI_MAX_DEV; device++) {
		int rc = pci_enumerate_device(domain, bus, device);
		if (rc < 0)
			return rc;
		visited += rc;
	}

	return visited;
}

static int pci_enumerate(void) {
	int visited = 0;
	for (u16 domain = 0; domain < U16_MAX; domain++) {
		for (u32 bus = 0; bus < PCI_MAX_BUS; bus++) {
			int rc = pci_enumerate_bus(domain, bus);
			if (rc < 0)
				break;
			visited += rc;
		}
	}

	return visited;
}

void pci_init(void) {
	int err = module_load("pci");
	if (unlikely(err == 0 && !pci_hooks))
		printk(PRINTK_ERR "PCI module didn't set PCI hooks!\n");

	printk("pci: Found %d devices\n", pci_enumerate());
}
