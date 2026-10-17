#pragma once

#include <lunar/vfs.h>

struct chrdev;

struct chrdev_ops {
	int (*open)(struct chrdev*, int flags, const struct cred*);
	int (*close)(struct chrdev*, int flags, const struct cred*);
	int (*read)(struct chrdev*, struct uio*, int ioflags, const struct cred*);
	int (*write)(struct chrdev*, struct uio*, int ioflags, const struct cred*);
	int (*ioctl)(struct chrdev*, unsigned long req, void* arg, const struct cred*);
	void (*inactive)(struct chrdev*);
};

struct chrdev {
	int major, minor;
	const struct chrdev_ops* ops;
	void* private;
	bool __atomic dead;
	unsigned long __atomic refcnt;
	struct list_node link;
};

static inline void chrdev_hold(struct chrdev* chrdev) {
	atomic_fetch_add(&chrdev->refcnt, 1);
}

static inline void chrdev_release(struct chrdev* chrdev) {
	if (atomic_sub_fetch(&chrdev->refcnt, 1) == 0 && chrdev->ops->inactive)
		chrdev->ops->inactive(chrdev);
}

/**
 * @brief Register a character device
 *
 * @param chrdev The character device
 *
 * @retval 0 Successful
 * @retval -EEXIST Device with the same major/minor exists already
 */
int chrdev_register(struct chrdev* chrdev);

/**
 * @brief Unregister a character device
 *
 * @param chrdev The character device to unregister
 *
 * @retval 0 Successful
 * @retval -ENODEV Device doesn't exist in the registry
 */
int chrdev_unregister(struct chrdev* chrdev);

/**
 * @brief Get a character device by its major and minor numbers
 *
 * @param[in] major Major device number
 * @param[in] minor Minor device number
 * @param[out] out The character device return value
 *
 * @retval 0 Successful
 * @retval -ENXIO Device doesn't exist in registry
 */
int chrdev_get(int major, int minor, struct chrdev** out);

/**
 * @brief Get a device vnode from a chrdev/blkdev vnode
 *
 * @param[in] physical The chrdev/blkdev node, expected to be locked
 * @param[out] out Where the output vnode is stored
 * @param[in] cred The credentials
 *
 * @retval 0 Successful
 * @retval -ENXIO Device does not exist
 */
int devfs_get_node(struct vnode* physical, struct vnode** out, const struct cred* cred);
