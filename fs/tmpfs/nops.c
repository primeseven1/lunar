#include <lunar/compiler.h>
#include <lunar/lib/string.h>
#include "internal.h"

static int tmpfs_create(struct vnode* dir, const char* name, mode_t mode, int type, struct vnode** out, const struct cred* cred) {
	(void)cred;

	struct tmpfs_node* parent = dir->fs_priv;
	struct tmpfs_node* child;

	int err = hashtable_search(parent->children, name, strlen(name), &child);
	if (err == 0)
		return -EEXIST;

	struct tmpfs_node* tnode = kzalloc(sizeof(*tnode), MM_ZONE_NORMAL);
	if (!tnode)
		return -ENOMEM;

	tnode->name = kstrdup(name, MM_ZONE_NORMAL);
	if (!tnode->name) {
		err = -ENOMEM;
		goto err;
	}
	tnode->vnode.ops = dir->ops;
	tnode->vnode.type = type;
	tnode->vnode.fs_priv = tnode;
	tnode->parent = parent;
	tnode->attr.mode = mode;
	mutex_init(&tnode->vnode.lock);
	atomic_store(&tnode->vnode.refcount, 1);

	if (tnode->vnode.type == VNODE_TYPE_DIR) {
		tnode->children = hashtable_create(32, sizeof(struct tmpfs_node*));
		if (!tnode->children) {
			err = -ENOMEM;
			goto err;
		}
	}

	err = hashtable_insert(parent->children, name, strlen(name), &tnode);
	if (err)
		goto err;

	if (out) {
		*out = &tnode->vnode;
		vnode_ref(*out);
	}
	return 0;

err:
	if (tnode->name)
		kfree(tnode->name);
	if (tnode->children)
		hashtable_destroy(tnode->children);
	kfree(tnode);
	return err;
}

static int tmpfs_open(struct vnode* node, int flags, const struct cred* cred) {
	(void)node;
	(void)flags;
	(void)cred;
	return 0;
}

static int tmpfs_close(struct vnode* node, int flags, const struct cred* cred) {
	(void)node;
	(void)flags;
	(void)cred;
	return 0;
}

static int tmpfs_lookup(struct vnode* dir, const char* name, int flags, struct vnode** out, const struct cred* cred) {
	(void)cred;
	(void)flags;
	if (dir->type != VNODE_TYPE_DIR)
		return -ENOTDIR;
	
	struct tmpfs_node* tnode = dir->fs_priv;
	struct tmpfs_node* tmp;

	int err = 0;
	if (flags & VNODE_LOOKUP_FLAG_PARENT)
		tmp = tnode->parent ? tnode->parent : tnode;
	else
		err = hashtable_search(tnode->children, name, strlen(name), &tmp);

	if (err == 0) {
		*out = &tmp->vnode;
		vnode_ref(*out);
	}
	return err;
}

static ssize_t tmpfs_read(struct vnode* node, void* buf, size_t size, off_t off, int flags, const struct cred* cred) {
	(void)cred;
	(void)flags;

	struct tmpfs_node* tnode = node->fs_priv;
	if (off >= (ssize_t)tnode->attr.size)
		return 0;

	size_t to_copy = (off + size > tnode->attr.size) ? (tnode->attr.size - off) : size;
	memcpy(buf, tnode->data + off, to_copy);
	return to_copy;
}

static ssize_t tmpfs_write(struct vnode* node, const void* buf, size_t size, off_t off, int flags, const struct cred* cred) {
	(void)cred;
	(void)flags;

	struct tmpfs_node* tnode = node->fs_priv;
	size_t new_size = off + size;

	if (likely(new_size != tnode->attr.size)) {
		u8* new_data = krealloc(tnode->data, new_size, MM_ZONE_NORMAL);
		if (!new_data)
			return -ENOMEM;
		tnode->data = new_data;
		tnode->attr.size = new_size;
	}

	memcpy(tnode->data + off, buf, size);
	return size;
}

static int tmpfs_getattr(struct vnode* node, struct vattr* out, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* tnode = node->fs_priv;
	memcpy(out, &tnode->attr, sizeof(*out));
	return 0;
}

static int tmpfs_setattr(struct vnode* node, const struct vattr* attr, int attrs, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* tnode = node->fs_priv;
	if (attrs & VATTR_MODE)
		tnode->attr.mode = attr->mode;
	if (attrs & VATTR_UID)
		tnode->attr.uid = attr->uid;
	if (attrs & VATTR_GID)
		tnode->attr.gid = attr->gid;
	if (attrs & VATTR_ATIME)
		tnode->attr.atime = attr->atime;
	if (attrs & VATTR_MTIME)
		tnode->attr.mtime = attr->mtime;
	if (attrs & VATTR_CTIME)
		tnode->attr.ctime = attr->ctime;
	return 0;
}

static void tmpfs_release(struct vnode* node) {
	struct tmpfs_node* tn = node->fs_priv;
	if (tn->name)
		kfree(tn->name);
	kfree(tn);
}

const struct vnode_ops tmpfs_node_ops = {
	.lookup = tmpfs_lookup,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.open = tmpfs_open,
	.close = tmpfs_close,
	.create = tmpfs_create,
	.release = tmpfs_release,
	.setattr = tmpfs_setattr,
	.getattr = tmpfs_getattr
};
