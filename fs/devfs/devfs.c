#include <lunar/vfs.h>
#include <lunar/init.h>
#include <lunar/printk.h>
#include <lunar/device.h>
#include <lunar/slab.h>

struct devfs_node {
	struct vnode vnode;
	struct vnode* physical;
	struct chrdev* chrdev;
};

static int devfs_create(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)dvp;
	(void)name;
	(void)attr;
	(void)out;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_remove(struct vnode* dvp, struct vnode* vnode, const char* name, const struct cred* cred) {
	(void)dvp;
	(void)vnode;
	(void)name;
	(void)cred;
	return -ENOTDIR;
}

static inline struct chrdev* live_chrdev(struct vnode* vnode) {
	struct chrdev* chrdev = container_of(vnode, struct devfs_node, vnode)->chrdev;
	return atomic_load(&chrdev->dead) ? NULL : chrdev;
}

static int devfs_open(struct vnode** vnode, int flags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(*vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->open ? chrdev->ops->open(chrdev, flags, cred) : 0;
}

static int devfs_close(struct vnode* vnode, int flags, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	return dnode->chrdev->ops->close ? dnode->chrdev->ops->close(dnode->chrdev, flags, cred) : 0;
}

static int devfs_read(struct vnode* vnode, struct uio* uio, int ioflags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->read ? chrdev->ops->read(chrdev, uio, ioflags, cred) : -EINVAL;
}

static int devfs_write(struct vnode* vnode, struct uio* uio, int ioflags, const struct cred* cred) {
	struct chrdev* chrdev = live_chrdev(vnode);
	if (!chrdev)
		return -ENXIO;
	return chrdev->ops->write ? chrdev->ops->write(chrdev, uio, ioflags, cred) : -EINVAL;
}

static int devfs_lookup(struct vnode* ref, const char* name, struct vnode** out, const struct cred* cred) {
	(void)ref;
	(void)name;
	(void)out;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_sync(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int devfs_getattr(struct vnode* vnode, struct vattr* out, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	VOP_LOCK(dnode->physical);
	int err = VOP_GETATTR(dnode->physical, out, cred);
	VOP_UNLOCK(dnode->physical);
	return err;
}

static int devfs_setattr(struct vnode* vnode, const struct vattr* attr, int flags, const struct cred* cred) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	VOP_LOCK(dnode->physical);
	int err = VOP_SETATTR(dnode->physical, attr, flags, cred);
	VOP_UNLOCK(dnode->physical);
	return err;
}

static int devfs_link(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred* cred) {
	(void)dvp;
	(void)svp;
	(void)name;
	(void)cred;
	return -ENOTDIR;
}

static int devfs_symlink(struct vnode* dvp, const char* name, const char* target, const struct vattr* attr, const struct cred* cred) {
	(void)dvp;
	(void)name;
	(void)target;
	(void)attr;
	(void)cred;
	return -ENOTDIR;
}

static void devfs_freelink(char* str) {
	(void)str;
}

static int devfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount) {
	(void)dir;
	(void)buf;
	(void)count;
	(void)off;
	(void)out_rcount;
	return -ENOTDIR;
}

/* Let devices do their own locking */

static int devfs_lock(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int devfs_unlock(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static void devfs_inactive(struct vnode* vnode) {
	struct devfs_node* dnode = container_of(vnode, struct devfs_node, vnode);
	chrdev_release(dnode->chrdev);
	VOP_RELEASE(dnode->physical);
	kfree(dnode);
}

static const struct vnode_ops devfs_node_ops = {
	.create = devfs_create,
	.mkdir = devfs_create,
	.mknod = devfs_create,
	.rmdir = devfs_remove,
	.open = devfs_open,
	.close = devfs_close,
	.read = devfs_read,
	.write = devfs_write,
	.lookup = devfs_lookup,
	.sync = devfs_sync,
	.getattr = devfs_getattr,
	.setattr = devfs_setattr,
	.link = devfs_link,
	.symlink = devfs_symlink,
	.unlink = devfs_remove,
	.freelink = devfs_freelink,
	.getdents = devfs_getdents,
	.lock = devfs_lock,
	.unlock = devfs_unlock,
	.inactive = devfs_inactive
};

int devfs_get_node(struct vnode* physical, struct vnode** out, const struct cred* cred) {
	if (!physical || !out)
		return -EINVAL;
	if (physical->type == VTYPE_BLKDEV)
		return -ENXIO;
	if (physical->type != VTYPE_CHDEV)
		return -EINVAL;

	struct vattr physical_attrs;
	int err = VOP_GETATTR(physical, &physical_attrs, cred);
	if (err)
		return err;

	struct chrdev* chrdev;
	err = chrdev_get(physical_attrs.rdevmajor, physical_attrs.rdevminor, &chrdev);
	if (err)
		return err;

	struct devfs_node* dnode = kmalloc(sizeof(*dnode), MM_ZONE_NORMAL);
	if (!dnode) {
		chrdev_release(chrdev);
		return -ENOMEM;
	}

	VOP_INIT(&dnode->vnode, physical->type, &devfs_node_ops, 0, physical->belongs_to);
	VOP_HOLD(physical); /* vfs_open() drops the lookup ref after the swap */
	dnode->physical = physical;
	dnode->chrdev = chrdev;

	*out = &dnode->vnode;
	return 0;
}

static const struct filesystem_type* tmpfs_type;

static int devfs_mnt_mount(struct vnode* backing, void* data, struct mount** out) {
	return tmpfs_type->mount(backing, data, out);
}

static int devfs_mnt_unmount(struct mount* mount) {
	return tmpfs_type->unmount(mount);
}

static int devfs_mnt_sync(struct mount* mount) {
	return tmpfs_type->sync(mount);
}

static int devfs_mnt_statfs(struct mount* mount, struct fsattr* attr) {
	return tmpfs_type->statfs(mount, attr);
}

static const struct filesystem_type __filesystem_type devfs_type = {
	.name = "devfs",
	.mount = devfs_mnt_mount,
	.unmount = devfs_mnt_unmount,
	.sync = devfs_mnt_sync,
	.statfs = devfs_mnt_statfs
};

static void devfs_init(void) {
	int err = vfs_get_filesystem_type("tmpfs", &tmpfs_type); 
	if (err) {
		printk(PRINTK_ERR "devfs: Failed to get tmpfs type: %d\n", err);
		return;
	}

	err = vfs_register(&devfs_type);
	if (err)
		printk(PRINTK_ERR "devfs: Failed to register type: %d\n", err);
}

INIT_TASK_DECLARE(vfs_init_task, tmpfs_init_task);
INIT_TASK_DEFINE(devfs_init_task, INIT_TASK_SCOPE_BSP, devfs_init, &vfs_init_task, &tmpfs_init_task);
