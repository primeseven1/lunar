#include <lunar/core/panic.h>
#include "ec.h"

static inline uacpi_u8 raw_read(struct acpi_gas* gas) {
	uacpi_u64 value;
	bug(uacpi_gas_read(gas, &value) != UACPI_STATUS_OK);
	return (u8)value;
}

static inline void raw_write(struct acpi_gas* gas, uacpi_u8 value) {
	bug(uacpi_gas_write(gas, value) != UACPI_STATUS_OK);
}

static inline void raw_wait(struct acpi_gas* gas, uacpi_u8 bit, uacpi_u8 value) {
	u8 reg;
	do {
		reg = raw_read(gas);
		cpu_relax();
	} while ((reg & bit) != value);
}

uacpi_u8 ec_device_read(struct ec_device* device, uacpi_u8 off) {
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_READ);
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->data, off);
	raw_wait(&device->ctrl, EC_OBF, EC_OBF);
	return raw_read(&device->data);
}

void ec_device_write(struct ec_device* device, uacpi_u8 off, uacpi_u8 value) {
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_WRITE);
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->data, off);
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->data, value);
}

uacpi_bool ec_check_event(struct ec_device* device, uacpi_u8* index) {
	if (!(raw_read(&device->ctrl) & EC_SCI_EVT))
		return UACPI_FALSE;

	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_QUERY);
	raw_wait(&device->ctrl, EC_OBF, EC_OBF);
	*index = raw_read(&device->data);

	return UACPI_TRUE;
}

void ec_burst_enable(struct ec_device* device) {
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_BURST_ENABLE);
	while (!(raw_read(&device->ctrl) & EC_BURST))
		cpu_relax();
}

void ec_burst_disable(struct ec_device* device) {
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_BURST_DISABLE);
	while (raw_read(&device->ctrl) & EC_BURST)
		cpu_relax();
}

uacpi_bool ec_verify_order(struct ec_device* device) {
	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->ctrl, EC_READ);

	if (!(raw_read(&device->ctrl) & EC_IBF))
		return UACPI_FALSE;

	raw_wait(&device->ctrl, EC_IBF, 0);
	raw_write(&device->data, 0x00);

	raw_wait(&device->ctrl, EC_OBF, EC_OBF);
	raw_read(&device->data);

	return UACPI_TRUE;
}
