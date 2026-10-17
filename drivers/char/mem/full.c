#include <lunar/device.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/init.h>

static int full_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_zero(uio->resid, uio);
	return 0;
}

static int full_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)uio;
	(void)ioflags;
	(void)cred;
	return -ENOSPC;
}

static void full_inactive(struct chrdev* chrdev) {
	(void)chrdev;
	bug("Unreachable");
}

static const struct chrdev_ops full_ops = {
	.open = NULL,
	.close = NULL,
	.read = full_read,
	.write = full_write,
	.ioctl = NULL,
	.inactive = full_inactive
};

static struct chrdev full_chrdev = {
	.major = 1,
	.minor = 7,
	.ops = &full_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static void full_device_init(void) {
	int err = chrdev_register(&full_chrdev);
	if (err)
		printk(PRINTK_ERR "full: Failed to register full device\n");
}

INIT_TASK_DECLARE(device_init_task);
INIT_TASK_DEFINE(full_device_init_task, INIT_TASK_SCOPE_BSP, full_device_init, &device_init_task);
