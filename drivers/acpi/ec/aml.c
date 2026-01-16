#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <lunar/mm/heap.h>
#include "ec.h"

struct ec_query {
	struct ec_device* device;
	uacpi_u8 index;
};

irqflags_t ec_lock(struct ec_device* device, uacpi_u32* out_seq) {
	if (device->needs_fw_lock)
		uacpi_acquire_global_lock(0xFFFF, out_seq);
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&device->lock, &irq_flags);
	return irq_flags;
}

void ec_unlock(struct ec_device* device, irqflags_t irq_flags, uacpi_u32 seq) {
	spinlock_unlock_irq_restore(&device->lock, &irq_flags);
	if (device->needs_fw_lock)
		uacpi_release_global_lock(seq);
}

static void handle_ec_query(uacpi_handle handle) {
	struct ec_query* query = handle;

	const char* hex_chars = "0123456789ABCDEF";
	char method_name[5] = { '_', 'Q', hex_chars[(query->index >> 4) & 0xF], hex_chars[query->index & 0xF], '\0' };
	uacpi_eval(query->device->node, method_name, UACPI_NULL, UACPI_NULL);
	uacpi_finish_handling_gpe(NULL, query->device->gpe_index);

	kfree(query);
}

static uacpi_status ec_rw(uacpi_region_op op, uacpi_region_rw_data* data) {
	if (data->byte_width != 1)
		return UACPI_STATUS_INVALID_ARGUMENT;

	struct ec_device* device = data->handler_context;
	uacpi_u32 seq;
	irqflags_t irq_flags = ec_lock(device, &seq);

	ec_burst_enable(device);
	if (op == UACPI_REGION_OP_READ)
		data->value = ec_device_read(device, data->offset);
	else if (op == UACPI_REGION_OP_WRITE)
		ec_device_write(device, data->offset, data->value);
	ec_burst_disable(device);

	ec_unlock(device, irq_flags, seq);
	return UACPI_STATUS_OK;
}

uacpi_status ec_handle_region(uacpi_region_op op, uacpi_handle op_data) {
	switch (op) {
	case UACPI_REGION_OP_ATTACH:
	case UACPI_REGION_OP_DETACH:
		return UACPI_STATUS_OK;
	case UACPI_REGION_OP_READ:
	case UACPI_REGION_OP_WRITE:
		return ec_rw(op, op_data);
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}
}

uacpi_interrupt_ret ec_handle_event(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u16 index) {
	(void)index;
	(void)node;

	struct ec_device* device = ctx;
	uacpi_u32 seq;
	irqflags_t irq_flags = ec_lock(device, &seq);

	uacpi_interrupt_ret ret = UACPI_INTERRUPT_NOT_HANDLED | UACPI_GPE_REENABLE;

	/* Make sure there's actually an event that's happened */
	uacpi_u8 i;
	if (!ec_check_event(device, &i) || i == 0)
		goto out;

	struct ec_query* query = kmalloc(sizeof(*query), MM_ZONE_NORMAL | MM_ATOMIC);
	if (!query)
		goto out;
	query->device = device;
	query->index = i;

	/* GPE will be reenabled once the work finishes */
	if (uacpi_likely_success(uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, handle_ec_query, query)))
		ret = UACPI_INTERRUPT_HANDLED;
	else
		kfree(query);
out:
	ec_unlock(device, irq_flags, seq);
	return ret;
}
