#include <lunar/core/module.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/mm/heap.h>

#include <acpi/acpi.h>
#include <uacpi/uacpi.h>
#include <uacpi/event.h>
#include <uacpi/opregion.h>
#include <uacpi/resources.h>

#include "ec.h"

static const char* pnp_ids[] = {
	"PNP0C09",
	NULL
};

struct ec_init_ctx {
	struct acpi_gas ctrl, data;
	uacpi_bool need_ctrl, need_data;
};

static uacpi_bool install_handlers(struct ec_device* device) {
	uacpi_install_address_space_handler(device->node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_handle_region, device);
	uacpi_u64 value;

	uacpi_status status = uacpi_eval_simple_integer(device->node, "_GLK", &value);
	device->needs_fw_lock = (status == UACPI_STATUS_OK && value) ? UACPI_TRUE : UACPI_FALSE;

	status = uacpi_eval_simple_integer(device->node, "_GPE", &value);
	if (uacpi_unlikely_error(status)) {
		printk(PRINTK_ERR "ec: EC has no GPE\n");
		return UACPI_FALSE;
	}
	device->gpe_index = value;
	
	status = uacpi_install_gpe_handler(NULL, device->gpe_index, UACPI_GPE_TRIGGERING_LEVEL, ec_handle_event, device);
	bug(status != UACPI_STATUS_OK);
	return UACPI_TRUE;
}

/* Looks for the data and control IO addresses */
static uacpi_iteration_decision ec_resource_it(void* user, uacpi_resource* resource) {
	struct ec_init_ctx* ctx = user;
	struct acpi_gas* reg = ctx->need_data ? &ctx->data : &ctx->ctrl;
	uacpi_bool* need = ctx->need_data ? &ctx->need_data : &ctx->need_ctrl;

	switch (resource->type) {
	case UACPI_RESOURCE_TYPE_IO:
		reg->address = resource->io.minimum;
		reg->register_bit_width = resource->io.length * 8;
		break;
	case UACPI_RESOURCE_TYPE_FIXED_IO:
		reg->address = resource->fixed_io.address;
		reg->register_bit_width = resource->fixed_io.length * 8;
		break;
	default:
		return UACPI_ITERATION_DECISION_CONTINUE;
	}

	*need = false;
	reg->access_size = 0;
	reg->register_bit_offset = 0;
	reg->address_space_id = UACPI_ADDRESS_SPACE_SYSTEM_IO;

	/* This function looks for need_data and need_ctrl in that order, so only check need_ctrl */
	return !ctx->need_ctrl ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

static int ec_probe(uacpi_namespace_node* node, uacpi_namespace_node_info* info) {
	(void)info;
	static bool found_real = false; /* Sometimes the EC is described multiple times */
	if (found_real)
		return 0;

	struct ec_device* device = kmalloc(sizeof(*device), MM_ZONE_NORMAL);
	if (!device)
		return -ENOMEM;

	device->node = node;
	spinlock_init(&device->lock);

	const uacpi_char* device_path = uacpi_namespace_node_generate_absolute_path(node);

	/* Find the EC IO port addresses */
	uacpi_resources* resources;
	uacpi_status status = uacpi_get_current_resources(node, &resources);
	if (status != UACPI_STATUS_OK) {
		printk(PRINTK_ERR "ec: Device %s has no resources\n", device_path);
		goto err;
	}

	struct ec_init_ctx init_ctx = { .need_ctrl = true, .need_data = true };
	status = uacpi_for_each_resource(resources, ec_resource_it, &init_ctx);
	uacpi_free_resources(resources);
	if (init_ctx.need_data || init_ctx.need_ctrl || status != UACPI_STATUS_OK) {
		printk(PRINTK_ERR "ec: Device %s doesn't have all ports\n", device_path);
		goto err;
	}
	device->ctrl = init_ctx.ctrl;
	device->data = init_ctx.data;

	uacpi_u32 seq;
	irqflags_t irq_flags = ec_lock(device, &seq);

	/* Make sure the ports aren't swapped, since the namespace doesn't guaruntee ordering */
	if (!ec_verify_order(device)) {
		struct acpi_gas tmp = device->data;
		device->data = device->ctrl;
		device->ctrl = tmp;
		if (uacpi_unlikely(!ec_verify_order(device))) {
			printk(PRINTK_ERR "ec: Device %s ports invalid\n", device_path);
			ec_unlock(device, irq_flags, seq);
			goto err;
		}
	}

	ec_unlock(device, irq_flags, seq);

	if (!install_handlers(device))
		return -ENODEV;

	found_real = true;
	ec_init_events();
	uacpi_enable_gpe(UACPI_NULL, device->gpe_index);

	printk("ec: Device %s at IO %lx,%lx GPE %u\n", device_path, device->data.address, device->ctrl.address, device->gpe_index);
	uacpi_free_absolute_path(device_path);

	return 0;
err:
	uacpi_free_absolute_path(device_path);
	kfree(device);
	return -ENODEV;
}

struct acpi_driver ec_driver = {
	.name = "ec",
	.pnp_ids = pnp_ids,
	.probe = ec_probe
};

static int ec_module_init(void) {
	acpi_driver_register(&ec_driver);
	return 0;
}

MODULE("acpi_ec", INIT_STATUS_SCHED, ec_module_init);
