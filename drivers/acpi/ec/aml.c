#include <lunar/core/printk.h>
#include <lunar/core/panic.h>

#include <uacpi/event.h>
#include <uacpi/opregion.h>

#include "ec.h"

static uacpi_u8 get_event(struct ec_device* device) {
	uacpi_u32 seq;
	irqflags_t irq_flags = ec_lock(device, &seq);
	uacpi_u8 index;
	uacpi_bool event = ec_check_event(device, &index);
	ec_unlock(device, irq_flags, seq);
	return event ? index : 0;
}

static void handle_ec_query(uacpi_handle handle) {
	struct ec_device* device = handle;

	uacpi_u8 event = get_event(device);
	if (event) {
		const char* hex_chars = "0123456789ABCDEF";
		char method_name[5] = { '_', 'Q', hex_chars[(event >> 4) & 0xF], hex_chars[event & 0xF], '\0' };
		uacpi_eval(device->node, method_name, UACPI_NULL, UACPI_NULL);
	}

	uacpi_finish_handling_gpe(NULL, device->gpe_index);
}

static uacpi_status ec_aml_rw(uacpi_region_op op, uacpi_region_rw_data* data) {
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

static uacpi_status ec_aml_handle_region(uacpi_region_op op, uacpi_handle op_data) {
	switch (op) {
	case UACPI_REGION_OP_ATTACH:
	case UACPI_REGION_OP_DETACH:
		return UACPI_STATUS_OK;
	case UACPI_REGION_OP_READ:
	case UACPI_REGION_OP_WRITE:
		return ec_aml_rw(op, op_data);
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}
}

static uacpi_interrupt_ret ec_handle_event_irq(uacpi_handle ctx, uacpi_namespace_node* node, uacpi_u16 index) {
	(void)index;
	(void)node;
	struct ec_device* device = ctx;
	uacpi_status status = uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, handle_ec_query, device);
	return (status == UACPI_STATUS_OK) ? UACPI_INTERRUPT_HANDLED : UACPI_INTERRUPT_NOT_HANDLED | UACPI_GPE_REENABLE;
}

void ec_install_handlers(struct ec_device* device) {
	bug(uacpi_install_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_aml_handle_region, device) != UACPI_STATUS_OK);
	bug(uacpi_install_gpe_handler(NULL, device->gpe_index, UACPI_GPE_TRIGGERING_LEVEL, ec_handle_event_irq, device) != UACPI_STATUS_OK);
}
