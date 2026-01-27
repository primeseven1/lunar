#include <lunar/core/vfs.h>
#include <lunar/core/panic.h>
#include <lunar/lib/string.h>
#include <lunar/lib/hashtable.h>
#include <lunar/sched/kthread.h>

/* FIXME: This code is NOT very well tested!!!!! */

static struct hashtable* fs_table;
static struct vnode* vfs_root = NULL;

int vfs_register(const struct filesystem_type* type) {
	return hashtable_insert(fs_table, type->name, strlen(type->name), &type);
}

int vfs_open(struct vnode* node, int flags) {
	if (node->type == VNODE_TYPE_DIR)
		return -EISDIR;
	if (!node->ops || !node->ops->open)
		return -ENOSYS;

	mutex_lock(&node->lock);
	int ret = node->ops->open(node, flags, &current_thread()->proc->cred);
	mutex_unlock(&node->lock);

	return ret;
}

int vfs_close(struct vnode* node, int flags) {
	if (node->type == VNODE_TYPE_DIR)
		return -EISDIR;
	if (!node->ops || !node->ops->close)
		return -ENOSYS;

	mutex_lock(&node->lock);
	int ret = node->ops->close(node, flags, &current_thread()->proc->cred);
	mutex_unlock(&node->lock);

	return ret;
}

ssize_t vfs_read(struct vnode* node, void* buf, size_t size, off_t off, int flags) {
	if (node->type == VNODE_TYPE_DIR)
		return -EISDIR;
	if (!node->ops || !node->ops->read)
		return -ENOSYS;

	mutex_lock(&node->lock);
	int ret = node->ops->read(node, buf, size, off, flags, &current_thread()->proc->cred);
	mutex_unlock(&node->lock);

	return ret;
}

ssize_t vfs_write(struct vnode* node, const void* buf, size_t size, u64 off, int flags) {
	if (node->type == VNODE_TYPE_DIR)
		return -EISDIR;
	if (!node->ops || !node->ops->write)
		return -ENOSYS;

	mutex_lock(&node->lock);
	ssize_t ret = node->ops->write(node, buf, size, off, flags, &current_thread()->proc->cred);
	mutex_unlock(&node->lock);

	return ret;
}

int vfs_lookup(struct vnode* dir, const char* path, const char* lastcomp, int flags, struct vnode** out) {
	size_t pathlen = strlen(path);
	if (pathlen == 0)
		return -ENOENT;
	if (pathlen > PATHNAME_MAX || (lastcomp && strlen(lastcomp) > NAME_MAX))
		return -ENAMETOOLONG;

	int err = 0;

	struct vnode* cur = dir;
	if (!cur || *path == '/') {
		cur = vfs_root;
		if (!cur)
			return -ENOENT;
	}
	char* tokens = kstrdup(path, MM_ZONE_NORMAL);
	if (!tokens)
		return -ENOMEM;

	vnode_ref(cur);

	char* saveptr = NULL;
	for (char* token = strtok_r(tokens, "/", &saveptr); token != NULL; token = strtok_r(NULL, "/", &saveptr)) {
		if (flags & VNODE_LOOKUP_FLAG_PARENT && !lastcomp) {
			char* peek_save = saveptr;
			char* peek_token = strtok_r(NULL, "/", &peek_save);
			if (!peek_token)
				break;
		}

		if (cur->type != VNODE_TYPE_DIR) {
			err = -ENOTDIR;
			goto err_unref;
		}
		if (!cur->ops || !cur->ops->lookup) {
			err = -ENOSYS;
			goto err_unref;
		}

		if (strcmp(token, ".") == 0)
			continue;
		int lookup_flags = 0;
		bool dotdot = strcmp(token, "..") == 0;
		if (dotdot) {
			if (vfs_root == cur)
				continue;
			lookup_flags |= VNODE_LOOKUP_FLAG_PARENT;
		}

		struct vnode* next;
		mutex_lock(&cur->lock);

		err = cur->ops->lookup(cur, token, lookup_flags, &next, &current_thread()->proc->cred);	
		if (err)
			goto err_unlock;

		/* Handles mount crossing */
		if (next->type == VNODE_TYPE_DIR && next->mounted_here) {
			struct mount* mh = next->mounted_here;
			vnode_ref(mh->root);
			vnode_unref(next);
			next = mh->root;
		}

		/* Handle .. leaving a mounted root */
		if (cur == next && cur->mount && cur == cur->mount->root && cur->mount->parent) {
			struct vnode* p = cur->mount->parent;
			vnode_ref(p);
			next = p;
		}

		mutex_unlock(&cur->lock);
		vnode_unref(cur);
		cur = next;
	}

	if (lastcomp && !(flags & VNODE_LOOKUP_FLAG_PARENT)) {
		if (cur->type != VNODE_TYPE_DIR) {
			err = -ENOTDIR;
		} else if (!cur->ops || !cur->ops->lookup) {
			err = -ENOSYS;
		} else {
			struct vnode* next;
			mutex_lock(&cur->lock);
			err = cur->ops->lookup(cur, lastcomp, 0, &next, &current_thread()->proc->cred);
			mutex_unlock(&cur->lock);
			if (err)
				goto err_unref;
			vnode_unref(cur);
			cur = next;
		}
	}

	*out = cur;
	kfree(tokens);
	return 0;
err_unlock:
	mutex_unlock(&cur->lock);
err_unref:
	vnode_unref(cur);
	kfree(tokens);
	return err;
}

static int sanitize_path_and_get_parent(char* path, int type, struct vnode* dir, struct vnode** out_parent, char** out_base) {
	int err = 0;

	char* trailer = strrchr(path, '/');
	if (trailer && trailer[1] == '\0') {
		if (type != VNODE_TYPE_DIR) {
			return -EINVAL;
		}
		while (trailer > path && *trailer == '/')
			*trailer-- = '\0';
	}

	struct vnode* parent = dir;
	char* base = strrchr(path, '/');

	if (base) {
		if (*++base == '\0' && type != VNODE_TYPE_DIR)
			return -ENOTDIR;
		err = vfs_lookup(dir, path, NULL, VNODE_LOOKUP_FLAG_PARENT, &parent);
		if (err)
			return err;
	} else {
		base = path;
		if (!parent) {
			parent = vfs_root;
			if (unlikely(!parent))
				return -ENOENT;
			vnode_ref(parent);
		}
	}

	*out_parent = parent;
	*out_base = base;

	return 0;
}

int vfs_create(struct vnode* dir, const char* path, mode_t mode, int type, struct vnode** out) {
	if (type < VNODE_TYPE_MIN || type > VNODE_TYPE_MAX)
		return -EINVAL;

	struct vnode* parent = NULL;
	char* base;
	char* sanitized = kstrdup(path, MM_ZONE_NORMAL);
	if (!sanitized)
		return -ENOMEM;

	int err = sanitize_path_and_get_parent(sanitized, type, dir, &parent, &base);
	if (err)
		goto out;

	if (parent->type != VNODE_TYPE_DIR) {
		err = -ENOTDIR;
		goto out;
	} else if (!parent->ops || !parent->ops->create) {
		err = -ENOSYS;
		goto out;
	}

	mutex_lock(&parent->lock);
	err = parent->ops->create(parent, base, mode, type, out, &current_thread()->proc->cred);
	mutex_unlock(&parent->lock);

out:
	if (parent)
		vnode_unref(parent);
	kfree(sanitized);
	return err;
}

static inline int mp_good(struct vnode* vnode) {
	if (vnode->type != VNODE_TYPE_DIR)
		return -ENOTDIR;
	if (vnode->mounted_here)
		return -EBUSY;
	return 0;
}

int vfs_mount(const char* mp, const char* fs_name, struct vnode* backing, void* data) {
	if (backing && backing->type == VNODE_TYPE_DIR)
		return -EISDIR;

	const struct filesystem_type* type;
	if (hashtable_search(fs_table, fs_name, strlen(fs_name), &type))
		return -ENODEV;
	if (!type->mount)
		return -ENOSYS;

	int err = 0;

	struct vnode* mp_vnode = NULL;
	bool is_root = strcmp(mp, "/") == 0;
	if (!is_root) {
		err = vfs_lookup(NULL, mp, NULL, 0, &mp_vnode);
		if (err)
			return err;
	} else if (vfs_root) {
		return -EBUSY;
	}

	if (mp_vnode)
		mutex_lock(&mp_vnode->lock);

	struct mount* m = kzalloc(sizeof(*m), MM_ZONE_NORMAL);
	if (!m) {
		err = -ENOMEM;
		goto err_unlock;
	}

	m->type = type;
	err = type->mount(m, backing, data);
	if (err)
		goto err_free;
	if (unlikely(!m->root)) {
		err = -EFAULT;
		goto err_free;
	}

	m->root->mount = m;
	vnode_ref(m->root);

	if (is_root) {
		m->parent = NULL;
		vfs_root = m->root;
		vnode_ref(vfs_root);
	} else {
		m->parent = mp_vnode;
		vnode_ref(mp_vnode);
		mp_vnode->mounted_here = m;
	}
	
	if (mp_vnode) {
		mutex_unlock(&mp_vnode->lock);
		vnode_unref(mp_vnode);
	}

	return 0;
err_free:
	if (m) {
		if (m->root) {
			if (type->unmount)
				type->unmount(m);
			vnode_unref(m->root);
		}
		kfree(m);
	}

err_unlock:
	if(mp_vnode) {
		mutex_unlock(&mp_vnode->lock);
		vnode_unref(mp_vnode);
	}
	if (is_root && m && vfs_root == m->root)
		vfs_root = NULL;

	return err;
}

int vfs_unmount(const char* mp) {
	bool is_root = strcmp(mp, "/") == 0;
	if (is_root)
		return -EPERM;

	struct vnode* mp_vnode = NULL;
	int err = vfs_lookup(NULL, mp, NULL, 0, &mp_vnode);
	if (err)
		return err;

	mutex_lock(&mp_vnode->lock);

	/* Make sure that the mount point is actually a mount point */
	if (mp_good(mp_vnode) != -EBUSY) {
		err = -EINVAL;
		goto out_unlock;
	}

	/* Now check to make sure the file system is able to be unmounted */
	struct mount* mnt = mp_vnode->mounted_here;
	if (atomic_load(&mnt->root->refcount) > 1) {
		err = -EBUSY;
		goto out_unlock;
	}
	if (!mnt->type->unmount) {
		err = -ENOSYS;
		goto out_unlock;
	}

	err = mnt->type->unmount(mnt);
	if (err)
		goto out_unlock;

	vnode_unref(mnt->root);
	mp_vnode->mounted_here = NULL;
	kfree(mnt);
out_unlock:
	mutex_unlock(&mp_vnode->lock);
	vnode_unref(mp_vnode);
	return err;
}

void vfs_init(void) {
	fs_table = hashtable_create(16, sizeof(struct filesystem_type*));
	if (unlikely(!fs_table))
		panic("Failed to create file system table");
}
