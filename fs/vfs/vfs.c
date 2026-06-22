#include <lunar/vfs.h>
#include <lunar/slab.h>
#include <lunar/hashtable.h>
#include <lunar/sched.h>
#include <lunar/rwlock.h>

static struct vnode_ops noops = {
	.lock = NULL,
	.unlock = NULL
};
static struct vnode vfs_root = {
	.type = VTYPE_DIR,
	.ops = &noops,
	.flags = VFLAG_ROOT,
	.belongs_to = NULL,
	.un.mounted_here = NULL,
	.refcnt = atomic_init(1),
	.mtx = MUTEX_INITIALIZER(vfs_root.mtx)
};
static LIST_HEAD_DEFINE(mounted_filesystems);
static MUTEX_DEFINE(mounted_filesystems_mtx);
static struct hashtable* fs_table;

static const struct filesystem_type* get_filesystem_type_from_name(const char* name) {
	const struct filesystem_type* ret;
	return (hashtable_search(fs_table, name, strlen(name), &ret) == 0) ? ret : NULL;
}

int vfs_register(const struct filesystem_type* fs_type) {
	const struct filesystem_type* tmp;
	if (hashtable_search(fs_table, fs_type->name, strlen(fs_type->name), &tmp) == 0)
		return -EEXIST;
	return hashtable_insert(fs_table, fs_type->name, strlen(fs_type->name), &fs_type);
}

int vfs_mount(const char* fs_name, struct vnode* ref, const char* mp, struct vnode* backing) {
	if (!fs_name)
		return -ENOSYS;
	const struct filesystem_type* type = get_filesystem_type_from_name(fs_name);
	if (!type)
		return -ENODEV;

	struct vnode* mp_vnode;
	int err = vfs_lookup(ref, mp, NULL, 0, &mp_vnode);
	if (err)
		return err;

	struct mount* mnt;
	err = type->mount(backing, NULL, &mnt);
	if (err) {
		VOP_UNLOCK(mp_vnode);
		VOP_RELEASE(mp_vnode);
		return err;
	}

	mnt->fs_type = type;
	mnt->covered = mp_vnode; /* Keep the ref from vfs_lookup() */
	atomic_store(&mnt->refcnt, 0);
	list_node_init(&mnt->link);
	mp_vnode->un.mounted_here = mnt;
	VOP_UNLOCK(mp_vnode); /* Unlock from vfs_lookup(), but don't unref the vnode here */

	mutex_acquire(&mounted_filesystems_mtx);
	list_add(&mounted_filesystems, &mnt->link);
	mutex_release(&mounted_filesystems_mtx);

	return 0;
}

static int create(struct vnode* ref, const char* path, enum vtype type, const struct vattr* attr, struct vnode** out) {
	struct vnode* parent;
	char* lastcomp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!lastcomp)
		return -ENOMEM;
	int err = vfs_lookup(ref, path, lastcomp, VFS_LOOKUP_PARENT, &parent);
	if (err)
		goto out;
	if (strcmp(lastcomp, ".") == 0 || strcmp(lastcomp, "..") == 0) {
		err = -EEXIST;
		goto cleanup;
	}

	struct vnode* _out;
	if (type == VTYPE_REGULAR)
		err = VOP_CREATE(parent, lastcomp, attr, &_out, current_cred());
	else if (type == VTYPE_DIR)
		err = VOP_MKDIR(parent, lastcomp, attr, &_out, current_cred());
	else
		err = -ENOSYS;
	if (err)
		goto cleanup;

	if (out) {
		*out = _out;
	} else {
		VOP_UNLOCK(_out); /* locked/ref'd by VOP_CREATE() */
		VOP_RELEASE(_out);
	}

cleanup:
	VOP_UNLOCK(parent); /* locked and ref'd by vfs_lookup() */
	VOP_RELEASE(parent);
out:
	kfree(lastcomp);
	return err;
}

static int remove(struct vnode* ref, const char* path, enum vtype type) {
	char* lastcomp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!lastcomp)
		return -ENOMEM;
	struct vnode* parent;
	int err = vfs_lookup(ref, path, lastcomp, VFS_LOOKUP_PARENT, &parent);
	if (err)
		goto cleanup;
	if (strcmp(lastcomp, ".") == 0 || strcmp(lastcomp, "..") == 0) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		if (type == VTYPE_DIR)
			err = -EBUSY;
		else
			err = -EISDIR;
		goto cleanup;
	}

	struct vnode* child;
	err = VOP_LOOKUP(parent, lastcomp, &child, current_cred());
	if (err) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		goto cleanup;
	}
	switch (type) {
	case VTYPE_REGULAR:
	case VTYPE_LINK:
		if (child->type != VTYPE_REGULAR && child->type != VTYPE_LINK)
			err = (child->type == VTYPE_DIR) ? -EISDIR : -EPERM;
		else
			err = VOP_UNLINK(parent, child, lastcomp, current_cred());
		break;
	case VTYPE_DIR:
		if (child->type != VTYPE_DIR)
			err = -ENOTDIR;
		else
			err = VOP_RMDIR(parent, child, lastcomp, current_cred());
		break;
	default:
		err = -ENOSYS;
		break;
	}

	VOP_UNLOCK(child);
	VOP_UNLOCK(parent);
	VOP_RELEASE(child);
	VOP_RELEASE(parent);
cleanup:
	kfree(lastcomp);
	return err;
}

int vfs_create(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out) {
	return create(ref, path, VTYPE_REGULAR, attr, out);
}

int vfs_mkdir(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out) {
	return create(ref, path, VTYPE_DIR, attr, out);
}

int vfs_rmdir(struct vnode* ref, const char* path) {
	return remove(ref, path, VTYPE_DIR);
}

int vfs_open(struct vnode* ref, const char* path, int flags, struct vnode** out) {
	if (!path || !out)
		return -EINVAL;

	struct vnode* tmp;
	int err = vfs_lookup(ref, path, NULL, 0, &tmp);
	if (err)
		return err;

	struct vnode* new = tmp; /* VOP_OPEN might change this */
	err = VOP_OPEN(&new, flags, current_cred());
	VOP_UNLOCK(tmp); /* Locked by vfs_lookup() */

	if (err) {
		VOP_RELEASE(tmp); /* Ref from vfs_lookup */
	} else {
		if (new != tmp)
			VOP_RELEASE(tmp); /* VOP_OPEN gave a different node, so drop the lookup ref */
		*out = new;
	}

	return err;
}

int vfs_close(struct vnode* vnode, int flags) {
	if (!vnode)
		return -EINVAL;
	VOP_LOCK(vnode);
	int err = VOP_CLOSE(vnode, flags, current_cred());
	VOP_UNLOCK(vnode);
	return err;
}

int vfs_read(struct vnode* vnode, void* buf, size_t count, off_t off, int flags, size_t* rcount) {
	if (!vnode)
		return -EINVAL;
	VOP_LOCK(vnode);
	int ret = VOP_READ(vnode, buf, count, off, flags, rcount, current_cred());
	VOP_UNLOCK(vnode);
	return ret;
}

int vfs_write(struct vnode* vnode, const void* buf, size_t count, off_t off, int flags, size_t* wcount) {
	if (!vnode)
		return -EINVAL;
	VOP_LOCK(vnode);
	int ret = VOP_WRITE(vnode, buf, count, off, flags, wcount, current_cred());
	VOP_UNLOCK(vnode);
	return ret;
}

int vfs_link(struct vnode* dref, const char* dpath, struct vnode* lref, const char* lpath, enum vtype type, const struct vattr* attr) {
	if (type != VTYPE_LINK && type != VTYPE_REGULAR)
		return -EINVAL;

	int err;
	struct vnode* target_node;
	if (type == VTYPE_REGULAR) {
		err = vfs_lookup(dref, dpath, NULL, 0, &target_node);
		if (err)
			return err;
	}

	char* comp = kmalloc(NAME_MAX + 1, MM_ZONE_NORMAL);
	if (!comp) {
		err = -ENOMEM;
		goto cleanup;
	}

	struct vnode* parent_node;
	err = vfs_lookup(lref, lpath, comp, VFS_LOOKUP_PARENT, &parent_node);
	if (err)
		goto cleanup;

	err = (type == VTYPE_REGULAR) ? VOP_LINK(parent_node, target_node, comp, current_cred()) : VOP_SYMLINK(parent_node, comp, dpath, attr, current_cred());
	VOP_UNLOCK(parent_node);
	VOP_RELEASE(parent_node);
cleanup:
	kfree(comp);
	if (type == VTYPE_REGULAR) {
		VOP_UNLOCK(target_node);
		VOP_RELEASE(target_node);
	}
	return err;
}

int vfs_unlink(struct vnode* ref, const char* path) {
	return remove(ref, path, VTYPE_REGULAR); /* VTYPE_REGULAR and VTYPE_LINK are treated the same, so just use regular */
}

/* Follow a mount point */
static void follow_mount(struct vnode** vnode) {
	bug((*vnode)->type != VTYPE_DIR);
	while (1) {
		struct mount* mnt = (*vnode)->un.mounted_here;
		if (!mnt)
			break;
		struct vnode* next = mnt->root;
		VOP_HOLD(next);
		VOP_UNLOCK(*vnode);
		VOP_RELEASE(*vnode);
		*vnode = next;
		VOP_LOCK(*vnode);
	}
}

/* Climbs out of a mounted file system */
static void dotdot_mount(struct vnode** vnode) {
	bug((*vnode)->type != VTYPE_DIR);
	while ((*vnode)->belongs_to && (*vnode)->belongs_to->covered) {
		struct vnode* next = (*vnode)->belongs_to->covered;
		VOP_HOLD(next);
		VOP_UNLOCK(*vnode);
		VOP_RELEASE(*vnode);
		*vnode = next;
		VOP_LOCK(*vnode);
	}
}

int vfs_lookup(struct vnode* ref, const char* path, char* lastcomp, int flags, struct vnode** out) {
	if (!path || !out)
		return -EINVAL;
	if ((flags & VFS_LOOKUP_LINK_DEPTH_MASK) > MAX_LINK_DEPTH)
		return -ELOOP;

	size_t pathlen = strlen(path);
	if (pathlen == 0)
		return -ENOENT;
	if (pathlen > PATHNAME_MAX)
		return -ENAMETOOLONG;

	struct proc* proc = current_proc();
	mutex_acquire(&proc->fs.mtx);
	if (*path == '/') {
		ref = proc->fs.root;
		while (*path == '/')
			path++;
	} else if (!ref) {
		ref = proc->fs.cwd;
	}
	VOP_HOLD(ref);
	VOP_LOCK(ref);
	mutex_release(&proc->fs.mtx);

	char* _path = kstrdup(path, MM_ZONE_NORMAL);
	if (!_path) {
		VOP_UNLOCK(ref);
		VOP_RELEASE(ref);
		return -ENOMEM;
	}

	int err = 0;
	char* saveptr = NULL;
	char* next_comp = (void*)-1;
	bool unlock_ref_on_err = true;
	for (char* comp = strtok_r(_path, "/", &saveptr); comp && next_comp; comp = next_comp) {
		if (strlen(comp) > NAME_MAX) {
			err = -ENAMETOOLONG;
			break;
		} else if (ref->type != VTYPE_DIR) {
			err = -ENOTDIR;
			break;
		} else if (!ref->ops || !ref->ops->lookup) {
			err = -ENOSYS;
			break;
		}
		next_comp = strtok_r(NULL, "/", &saveptr);

		const bool is_last = (next_comp == NULL);
		if (is_last && (flags & VFS_LOOKUP_PARENT)) {
			if (comp)
				strlcpy(lastcomp, comp, NAME_MAX + 1);
			else
				err = -EINVAL;
			break;
		}

		if (strcmp(comp, ".") == 0)
			continue;
		bool is_dotdot = (strcmp(comp, "..") == 0);
		if (is_dotdot) {
			mutex_acquire(&proc->fs.mtx);
			struct vnode* chroot = proc->fs.root;
			mutex_release(&proc->fs.mtx);
			if (ref == chroot)
				continue;
			if (ref->belongs_to && ref == ref->belongs_to->root && ref->belongs_to->covered) {
				struct vnode* next = ref->belongs_to->covered;
				VOP_HOLD(next);
				VOP_UNLOCK(ref);
				VOP_RELEASE(ref);
				ref = next;
				VOP_LOCK(ref);
			}
		}

		struct vnode* next;
		err = VOP_LOOKUP(ref, comp, &next, current_cred());
		if (err)
			break;

		if (is_dotdot)
			dotdot_mount(&next);
		else if (next->type == VTYPE_DIR)
			follow_mount(&next);

		if (next->type == VTYPE_LINK) {
			if (is_last && (flags & VFS_LOOKUP_NOFOLLOW)) {
				VOP_UNLOCK(ref);
				VOP_RELEASE(ref);
				ref = next;
				break;
			}

			char* target;
			err = VOP_READLINK(next, &target, current_cred());
			VOP_UNLOCK(next);
			VOP_RELEASE(next);
			if (err)
				break;

			VOP_UNLOCK(ref);
			unlock_ref_on_err = false;
			struct vnode* link_vnode;
			err = vfs_lookup(ref, target, NULL, (flags & VFS_LOOKUP_LINK_DEPTH_MASK) + 1, &link_vnode);
			VOP_FREELINK(next, target);
			if (err)
				break;

			VOP_RELEASE(ref);
			ref = link_vnode;
			if (is_last)
				break;

			/* Resume loop, symlink is now the target */
			unlock_ref_on_err = true;
			continue;
		}

		VOP_UNLOCK(ref);
		VOP_RELEASE(ref);
		ref = next;
	}

	if (err) {
		if (unlock_ref_on_err)
			VOP_UNLOCK(ref);
		VOP_RELEASE(ref);
	} else {
		*out = ref;
	}
	kfree(_path);
	return err;
}

void vfs_mount_root(void) {
	int err = vfs_mount("tmpfs", NULL, "/", NULL);
	if (err)
		panic("Failed to mount root file system");

	struct vnode* root = &vfs_root;
	VOP_HOLD(root); /* follow_mount() expects the vnode with a ref */
	VOP_LOCK(root); /* follow_mount() also expects a locked vnode */
	follow_mount(&root);
	VOP_HOLD(root);

	struct proc* proc = current_proc();
	mutex_acquire(&proc->fs.mtx);
	proc->fs.root = root;
	proc->fs.cwd = root;
	mutex_release(&proc->fs.mtx);

	VOP_UNLOCK(root);
}

static void vfs_init(void) {
	fs_table = hashtable_create(16, sizeof(struct filesystem_type*));
	if (!fs_table)
		out_of_memory();
	struct proc* proc = current_proc();
	proc->fs.root = &vfs_root;
	proc->fs.cwd = &vfs_root;
	VOP_HOLD(proc->fs.root);
	VOP_HOLD(proc->fs.cwd);
}

INIT_TASK_DECLARE(heap_init_task, sched_init_task);
INIT_TASK_DEFINE(vfs_init_task, INIT_TASK_SCOPE_BSP, vfs_init, &heap_init_task, &sched_init_task);
