#include <lunar/device.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/init.h>

static int null_read(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)uio;
	(void)ioflags;
	(void)cred;
	return 0;
}

static int null_write(struct chrdev* chrdev, struct uio* uio, int ioflags, const struct cred* cred) {
	(void)chrdev;
	(void)ioflags;
	(void)cred;
	uio_skip(uio->resid, uio);
	return 0;
}

static void null_inactive(struct chrdev* chrdev) {
	(void)chrdev;
	bug("Unreachable");
}

static const struct chrdev_ops null_ops = {
	.open = NULL,
	.close = NULL,
	.read = null_read,
	.write = null_write,
	.ioctl = NULL,
	.inactive = null_inactive
};

static struct chrdev null_chrdev = {
	.major = 1,
	.minor = 3,
	.ops = &null_ops,
	.private = NULL,
	.dead = atomic_init(false),
	.refcnt = atomic_init(1),
	.link = LIST_NODE_INITIALIZER
};

static void null_device_init(void) {
	int err = chrdev_register(&null_chrdev);
	if (err)
		printk(PRINTK_ERR "null: Failed to register null device\n");
}

INIT_TASK_DECLARE(device_init_task);
INIT_TASK_DEFINE(null_device_init_task, INIT_TASK_SCOPE_BSP, null_device_init, &device_init_task);
