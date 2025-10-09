#include <lunar/core/vfs.h>
#include <lunar/core/panic.h>
#include <lunar/mm/heap.h>
#include <lunar/lib/string.h>
#include <lunar/lib/hashtable.h>
#include <lunar/sched/kthread.h>

/* I have absolutely no idea how much of this code works */

int vfs_open(struct vfs_node* n, int flags) {
	if (!n)
		return -EINVAL;
	if (n->type == VFS_NODE_DIR)
		return -EISDIR;
	if (!n->ops || !n->ops->open)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->open(n, flags, &current_thread()->proc->cred);
	mutex_unlock(&n->lock);

	return ret;
}

int vfs_close(struct vfs_node* n, int flags) {
	if (!n)
		return -EINVAL;
	if (n->type == VFS_NODE_DIR)
		return -EISDIR;
	if (!n->ops || !n->ops->close)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->close(n, flags, &current_thread()->proc->cred);
	mutex_unlock(&n->lock);

	return ret;
}

ssize_t vfs_read(struct vfs_node* n, void* buf, size_t size, u64 off, int flags) {
	if (!n)
		return -EINVAL;
	if (n->type == VFS_NODE_DIR)
		return -EISDIR;
	if (!n->ops || !n->ops->read)
		return -ENOSYS;

	mutex_lock(&n->lock);
	int ret = n->ops->read(n, buf, size, off, flags, &current_thread()->proc->cred);
	mutex_unlock(&n->lock);

	return ret;
}

ssize_t vfs_write(struct vfs_node* n, const void* buf, size_t size, u64 off, int flags) {
	if (!n)
		return -EINVAL;
	if (n->type == VFS_NODE_DIR)
		return -EISDIR;
	if (!n->ops || !n->ops->write)
		return -ENOSYS;

	mutex_lock(&n->lock);
	ssize_t ret = n->ops->write(n, buf, size, off, flags, &current_thread()->proc->cred);
	mutex_unlock(&n->lock);

	return ret;
}

struct mount_point {
	struct vfs_superblock* sb;
	struct vfs_node* mp, *parent;
};

static struct vfs_node _vfs_root = {
	.type = VFS_NODE_DIR,
	.fs_priv = NULL,
	.lock = MUTEX_INITIALIZER(_vfs_root.lock),
	.refcount = atomic_init(1),
	.ops = NULL
};
static struct vfs_node* vfs_root = &_vfs_root;
static struct vfs_superblock* vfs_root_sb = NULL;

static struct hashtable* fs_table;
static struct hashtable* mount_table;

int vfs_lookup(struct vfs_node* dir, const char* name, struct vfs_node** out) {
	if (!name || !out)
		return -EINVAL;

	struct vfs_node* cur;
	if (name[0] == '/' || !dir) {
		if (vfs_root_sb)
			cur = vfs_root_sb->root;
		else
			cur = vfs_root;
	} else {
		cur = dir;
	}
	vfs_node_get(cur);

	char* path = kstrdup(name, MM_ZONE_NORMAL);
	if (!path) {
		vfs_node_put(cur);
		return -ENOMEM;
	}

	char* saveptr = NULL;
	char* token;
	struct vfs_node* next = NULL;

	int err = 0;
	for (token = strtok_r(path, "/", &saveptr); token != NULL; token = strtok_r(NULL, "/", &saveptr)) {
		if (strcmp(token, ".") == 0)
			continue;

		mutex_lock(&cur->lock);

		if (unlikely(!cur->ops || !cur->ops->lookup)) {
			err = -ENOSYS;
			goto err;
		}

		if (strcmp(token, "..") == 0) {
			struct vfs_node* parent;
			err = cur->ops->lookup(cur, NULL, VFS_LOOKUP_PARENT, &parent, &current_thread()->proc->cred);
			if (err)
				goto err;
			if (cur == parent)
				next = cur->mp_parent;
		} else {
			err = cur->ops->lookup(cur, token, 0, &next, &current_thread()->proc->cred);
			if (err)
				goto err;
		}

		mutex_unlock(&cur->lock);
		vfs_node_put(cur);
		cur = next;
	}

	*out = cur;
	kfree(path);
	return 0;
err:
	kfree(path);
	mutex_unlock(&cur->lock);
	vfs_node_put(cur);
	return err;
}

int vfs_mount(const char* fs_name, const char* mp, struct vfs_node* backing, void* data) {
	if (backing && backing->type == VFS_NODE_DIR)
		return -EISDIR;

	struct filesystem_type type;
	int err = hashtable_search(fs_table, fs_name, strlen(fs_name), &type);
	if (err)
		return -ENODEV;

	struct vfs_node* target;
	err = vfs_lookup(NULL, mp, &target);
	if (err)
		return err;
	if (target->type != VFS_NODE_DIR)
		return -ENOTDIR;

	struct mount_point* mp_struct = kmalloc(sizeof(*mp_struct), MM_ZONE_NORMAL);
	if (!mp_struct) {
		vfs_node_put(target);
		return -ENOMEM;
	}

	mutex_lock(&target->lock);

	struct vfs_superblock* sb;
	err = type.mount(backing, data, &sb, &current_thread()->proc->cred);
	if (err) {
		vfs_node_put(target);
		kfree(mp_struct);
		return -EIO;
	}

	err = hashtable_insert(mount_table, mp, strlen(mp), &mp);
	if (likely(err == 0)) {
		mp_struct->sb = sb;
		mp_struct->mp = target;
		if (target->ops->lookup(target, NULL, VFS_LOOKUP_PARENT, &mp_struct->parent, NULL))
			mp_struct->parent = vfs_root_sb->root;

		target->fs_priv = sb->root;
		target->mp_parent = mp_struct->parent;
		target->ops = sb->root->ops;
	} else {
		sb->ops->unmount(sb, &current_thread()->proc->cred);
	}

	mutex_unlock(&target->lock);
	return err;
}

int vfs_unmount(const char* target_path) {
	struct mount_point* mp;
	int err = hashtable_search(mount_table, target_path, strlen(target_path), &mp);
	if (err)
		return -ENOENT;
	if (!mp->sb->ops || !mp->sb->ops->unmount)
		return -ENOSYS;

	struct vfs_node* target;
	err = vfs_lookup(NULL, target_path, &target);
	if (err)
		return err;

	mutex_lock(&target->lock);

	if (mp->parent) {
		target->fs_priv = mp->parent;
		target->ops = mp->parent->ops;
		target->mp_parent = NULL;
	} else {
		target->fs_priv = vfs_root_sb->root;
		target->ops = vfs_root_sb->root->ops;
	}

	hashtable_remove(mount_table, target_path, strlen(target_path));
	err = mp->sb->ops->unmount(mp->sb, &current_thread()->proc->cred);
	kfree(mp);

	mutex_unlock(&target->lock);
	vfs_node_put(target);

	return err;
}

int vfs_register(const struct filesystem_type* type) {
	return hashtable_insert(fs_table, type->name, strlen(type->name), type);
}

void vfs_init(void) {
	fs_table = hashtable_create(20, sizeof(struct filesystem_type));
	if (unlikely(!fs_table))
		panic("Failed to create file system table");
	mount_table = hashtable_create(20, sizeof(char*));
	if (unlikely(!mount_table))
		panic("Failed to create mount table");
}
