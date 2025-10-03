#include <lunar/core/vfs.h>
#include <lunar/mm/heap.h>
#include <lunar/lib/string.h>
#include <lunar/lib/hashtable.h>

/* I have no idea if any of this works or not!! */

static inline void vfs_node_get(struct vfs_node* n) {
	atomic_add_fetch(&n->refcount, 1);
}

static inline void vfs_node_put(struct vfs_node* n) {
	if (atomic_sub_fetch(&n->refcount, 1) == 0)
		kfree(n);
}

int vfs_open(struct vfs_node* n, int flags) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->open)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->open(n, flags);
	mutex_unlock(&n->lock);

	return ret;
}

int vfs_close(struct vfs_node* n, int flags) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->close)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->close(n, flags);
	mutex_unlock(&n->lock);

	return ret;
}

ssize_t vfs_read(struct vfs_node* n, void* buf, size_t size, u64 off, int flags) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->read)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->read(n, buf, size, off, flags);
	mutex_unlock(&n->lock);

	return ret;
}

ssize_t vfs_write(struct vfs_node* n, const void* buf, size_t size, u64 off, int flags) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->write)
		return -ENOSYS;

	mutex_lock(&n->lock);
	ssize_t ret = n->ops->write(n, buf, size, off, flags);
	mutex_unlock(&n->lock);

	return ret;
}

int vfs_getattr(struct vfs_node* n, struct vfs_attr* attr) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->getattr)
		return -ENOSYS;
	return n->ops->getattr(n, attr);
}

int vfs_setattr(struct vfs_node* n, const struct vfs_attr* attr) {
	if (!n)
		return -EINVAL;
	if (!n->ops || !n->ops->setattr)
		return -ENOSYS;
	return n->ops->setattr(n, attr);
}

static struct vfs_superblock* root_fs = NULL;
static struct hashtable* fs_table = NULL;

int vfs_lookup(struct vfs_node* dir, const char* name, struct vfs_node** out) {
	if (!dir)
		return -EINVAL;
	if (dir->type != VFS_NODE_DIR)
		return -ENOTDIR;
	if (!dir->ops || !dir->ops->lookup)
		return -ENOSYS;

	char* tmp = kmalloc(strlen(name) + 1, MM_ZONE_NORMAL);
	if (!tmp)
		return -ENOMEM;

	struct vfs_node* cur;
	if (name[0] == '/')
		cur = root_fs->root;
	else
		cur = dir;

	vfs_node_get(cur);
	strcpy(tmp, name);

	char* saveptr;
	char* tok = strtok_r(tmp, "/", &saveptr);

	while (tok) {
		if (strcmp(tok, "..") == 0) {
			cur = cur->parent;
		} else if (strcmp(tok, ".") != 0) {
			struct vfs_node* next = NULL;
			int ret = cur->ops->lookup(cur, tok, &next);
			if (ret < 0) {
				vfs_node_put(cur);
				kfree(tmp);
				return ret;
			}

			vfs_node_put(cur);
			cur = next;
		}
		tok = strtok_r(NULL, "/", &saveptr);
	}

	kfree(tmp);
	*out = cur;
	return 0;
}

void vfs_init(void) {
	fs_table = hashtable_create(20, sizeof(struct filesystem_type));
}
