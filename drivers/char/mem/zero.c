#include <lunar/device.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/init.h>

static int zero_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_zero(uio->resid, uio);
	return 0;
}

static int zero_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_skip(uio->resid, uio);
	return 0;
}

static void zero_inactive(struct chrdev* chrdev) {
	(void)chrdev;
	bug("Unreachable");
}

static const struct chrdev_ops zero_ops = {
	.open = NULL,
	.close = NULL,
	.read = zero_read,
	.write = zero_write,
	.ioctl = NULL,
	.inactive = zero_inactive
};

static struct chrdev zero_chrdev = {
	.major = 1,
	.minor = 5,
	.ops = &zero_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static void zero_device_init(void) {
	int err = chrdev_register(&zero_chrdev);
	if (err)
		printk(PRINTK_ERR "zero: Failed to register zero device\n");
}

INIT_TASK_DECLARE(device_init_task);
INIT_TASK_DEFINE(zero_device_init_task, INIT_TASK_SCOPE_BSP, zero_device_init, &device_init_task);
