#pragma once

#include <lunar/asm/wrap.h>
#include <lunar/core/spinlock.h>
#include <uacpi/event.h>
#include <uacpi/io.h>

#define EC_OBF (1 << 0)
#define EC_IBF (1 << 1)
#define EC_BURST (1 << 4)
#define EC_SCI_EVT (1 << 5)
#define EC_SMI_EVT (1 << 7) /* Don't really care about this */

#define EC_READ 0x80
#define EC_WRITE 0x81
#define EC_BURST_ENABLE 0x82
#define EC_BURST_DISABLE 0x83
#define EC_QUERY 0x84

struct ec_device {
	uacpi_namespace_node* node;
	uacpi_u16 gpe_index;
	struct acpi_gas ctrl, data;
	uacpi_bool needs_fw_lock;
	spinlock_t lock;
};

static inline irqflags_t ec_lock(struct ec_device* device, uacpi_u32* out_seq) {
	if (device->needs_fw_lock)
		uacpi_acquire_global_lock(0xFFFF, out_seq);
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&device->lock, &irq_flags);
	return irq_flags;
}

static inline void ec_unlock(struct ec_device* device, irqflags_t irq_flags, uacpi_u32 seq) {
	spinlock_unlock_irq_restore(&device->lock, &irq_flags);
	if (device->needs_fw_lock)
		uacpi_release_global_lock(seq);
}

uacpi_bool ec_install_handlers(struct ec_device* device);

uacpi_u8 ec_device_read(struct ec_device* device, uacpi_u8 off);
void ec_device_write(struct ec_device* device, uacpi_u8 off, uacpi_u8 value);
uacpi_bool ec_check_event(struct ec_device* device, uacpi_u8* index);
void ec_burst_enable(struct ec_device* device);
void ec_burst_disable(struct ec_device* device);
uacpi_bool ec_verify_order(struct ec_device* device);

void ec_install_events(void);
