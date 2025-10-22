#include <lunar/mm/heap.h>
#include "internal.h"

static int tmpfs_mount(struct mount* mnt, struct vnode* backing, void* data) {
	(void)data;
	if (backing)
		return -ENOTSUP;
	struct tmpfs_node* root = kzalloc(sizeof(*root), MM_ZONE_NORMAL);
	if (!root)
		return -ENOMEM;

	root->name = NULL;
	root->vnode.ops = &tmpfs_node_ops;
	root->vnode.type = VNODE_TYPE_DIR;
	root->children = hashtable_create(32, sizeof(struct tmpfs_node*));
	mutex_init(&root->vnode.lock);
	root->vnode.fs_priv = root;

	mnt->root = &root->vnode;
	return 0;
}

static int tmpfs_unmount(struct mount* mnt) {
	kfree(mnt);
	return 0;
}

const struct filesystem_type __filesystem_type tmpfs_type = {
	.name = "tmpfs",
	.mount = tmpfs_mount,
	.unmount = tmpfs_unmount
};
