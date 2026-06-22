#include <lunar/slab.h>
#include <lunar/vmm.h>
#include <lunar/init.h>
#include <lunar/printk.h>
#include <lunar/hashtable.h>
#include <lunar/panic.h>
#include <lunar/vfs.h>
#include <lunar/timekeeper.h>
#include <lunar/string.h>
#include <lunar/trace.h>

static atomic(unsigned long) id = atomic_init(1);

struct tmpfs_block {
	struct vattr attr;
	union {
		struct {
			u8* data;
			size_t cap;
		} file;
		char symlink[PATHNAME_MAX + 1];
	} data;
	mutex_t mtx;
};

struct tmpfs_directory {
	struct vattr attr;
	struct hashtable* children;
};

struct tmpfs_node {
	struct vnode vnode;
	union {
		struct tmpfs_block* data;
		struct tmpfs_directory directory;
	} typedata;
};

struct tmpfs_mount {
	struct mount mount;
	unsigned long id;
};

static const struct vnode_ops tmpfs_node_ops_alias __attribute__((alias("tmpfs_node_ops")));
static struct slab_cache* tmpnode_cache;

static struct tmpfs_node* new_node(struct mount* mount, enum vtype type, int flags) {
	struct tmpfs_node* ret = slab_cache_alloc(tmpnode_cache);
	if (ret)
		VOP_INIT(&ret->vnode, type, &tmpfs_node_ops_alias, flags, mount);
	return ret;
}

static inline void destroy_node(struct tmpfs_node* tmpnode) {
	slab_cache_free(tmpnode_cache, tmpnode);
}

static int init_directory(struct tmpfs_node* tnode, const struct vattr* attr) {
	struct tmpfs_directory* dir = &tnode->typedata.directory;
	dir->children = hashtable_create(24, sizeof(struct tmpfs_node*));
	if (!dir->children)
		return -ENOMEM;
	dir->attr = *attr;
	return 0;
}

static int init_regular_or_symlink(struct tmpfs_node* tnode, const struct vattr* attr) {
	struct tmpfs_block* block = kzalloc(sizeof(*block), MM_ZONE_NORMAL);
	if (!block)
		return -ENOMEM;
	block->attr = *attr;
	mutex_init(&block->mtx);
	tnode->typedata.data = block;
	return 0;
}

static void destroy_directory(struct tmpfs_node* tnode) {
	hashtable_destroy(tnode->typedata.directory.children);
}

static void destroy_regular_or_symlink(struct tmpfs_node* tnode) {
	struct tmpfs_block* block = tnode->typedata.data;
	if (block->data.file.data)
		bug(vunmap(block->data.file.data, block->data.file.cap, 0, NULL) != 0);
	kfree(block);
}

static inline struct vattr tmpfs_sanitize_attr(const struct tmpfs_mount* tmount, enum vtype type, const struct vattr* attr) {
	return (struct vattr){
		.type = type,
		.mode = attr->mode, .uid = attr->uid, .gid = attr->gid,
		.fsid = tmount->id,
		.nlinks = (type == VTYPE_DIR) ? 2 : 1,
		.size = 0, .fsblocksize = PAGE_SIZE,
		.atime = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 }, /* atime/mtime/ctime unsupported for now */
		.mtime = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 },
		.ctime = (struct timespec){ .tv_sec = 0, .tv_nsec = 0 },
		.rdevmajor = 0, .rdevminor = 0, .devmajor = 0, .devminor = 0,
		.blocksused = 0
	};
}

static int check_args(const struct vnode* dvp, const char* name, const struct vattr* attr, bool allow_null_attr) {
	if (!dvp || !name || (!allow_null_attr && !attr) || strlen(name) == 0)
		return -EINVAL;
	if (strlen(name) > NAME_MAX)
		return -ENAMETOOLONG;
	if (dvp->type != VTYPE_DIR)
		return -ENOTDIR;
	return 0;
}

static bool exists(const struct tmpfs_node* tdvp, const char* name) {
	struct tmpfs_node* _tmp;
	return hashtable_search(tdvp->typedata.directory.children, name, strlen(name), &_tmp) == 0;
}

static int tmpfs_create(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)cred;
	int err = check_args(dvp, name, attr, false);
	if (err)
		return err;
	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	if (exists(tdvp, name))
		return -EEXIST;

	struct tmpfs_mount* tmount = container_of(dvp->belongs_to, struct tmpfs_mount, mount);
	struct tmpfs_node* tout = new_node(&tmount->mount, VTYPE_REGULAR, 0);
	if (!tout)
		return -ENOMEM;

	/* Create the entry */
	const struct vattr tattr = tmpfs_sanitize_attr(tmount, VTYPE_REGULAR, attr);
	err = init_regular_or_symlink(tout, &tattr);
	if (err) {
		destroy_node(tout);
		return err;
	}
	err = hashtable_insert(tdvp->typedata.directory.children, name, strlen(name), &tout);
	if (err) {
		destroy_regular_or_symlink(tout);
		destroy_node(tout);
		return err;
	}

	/* Now return the entry if wanted */
	if (out) {
		VOP_LOCK(&tout->vnode);
		*out = &tout->vnode;
	} else {
		VOP_RELEASE(&tout->vnode);
	}
	return 0;
}

static int tmpfs_mkdir(struct vnode* dvp, const char* name, const struct vattr* attr, struct vnode** out, const struct cred* cred) {
	(void)cred;
	int err = check_args(dvp, name, attr, false);
	if (err)
		return err;
	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	if (exists(tdvp, name))
		return -EEXIST;

	struct tmpfs_mount* tmount = container_of(dvp->belongs_to, struct tmpfs_mount, mount);
	struct tmpfs_node* tout = new_node(&tmount->mount, VTYPE_DIR, 0);
	if (!tout)
		return -ENOMEM;

	/* Create the entry */
	const struct vattr tattr = tmpfs_sanitize_attr(tmount, VTYPE_DIR, attr);
	err = init_directory(tout, &tattr);
	if (err) {
		destroy_node(tout);
		return err;
	}
	err = hashtable_insert(tout->typedata.directory.children, ".", 1, &tout);
	if (err)
		goto cleanup_directory;
	err = hashtable_insert(tout->typedata.directory.children, "..", 2, &tdvp);
	if (err)
		goto cleanup_directory;
	err = hashtable_insert(tdvp->typedata.directory.children, name, strlen(name), &tout);
	if (err)
		goto cleanup_directory;

	tdvp->typedata.directory.attr.nlinks++;
	if (out) {
		VOP_LOCK(&tout->vnode);
		*out = &tout->vnode;
	} else {
		VOP_RELEASE(&tout->vnode);
	}
	return 0;
cleanup_directory:
	destroy_directory(tout);
	destroy_node(tout);
	return err;
}

static foreach_iteration_desicion_t tmpfs_foreach_isempty(struct hashtable* table, struct hashtable_node* node, void* ctx) {
	(void)table;
	bool* empty = ctx;

	struct tmpfs_node* tnode;
	memcpy(&tnode, node->value, sizeof(struct tmpfs_node*));

	if (strcmp(node->key, ".") != 0 && strcmp(node->key, "..") != 0) {
		*empty = false;
		return FOREACH_ITERATION_DESICION_BREAK;
	}

	return FOREACH_ITERATION_DESICION_CONTINUE;
}

static int tmpfs_rmdir(struct vnode* dvp, struct vnode* vnode, const char* name, const struct cred* cred) {
	(void)cred;
	int err = check_args(dvp, name, NULL, true);
	if (err)
		return err;
	if (vnode->type != VTYPE_DIR)
		return -ENOTDIR;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	bool empty = true;
	hashtable_for_each_entry(tnode->typedata.directory.children, tmpfs_foreach_isempty, &empty);
	if (!empty)
		return -ENOTEMPTY;

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);

	/* Check to make sure the name resolves to the same child */
	struct tmpfs_node* _tmp;
	bug(hashtable_search(tdvp->typedata.directory.children, name, strlen(name), &_tmp) != 0);
	bug(_tmp != tnode);

	/* Now just remove the reference */
	err = hashtable_remove(tdvp->typedata.directory.children, name, strlen(name));
	if (err == 0) {
		tnode->typedata.directory.attr.nlinks -= 2; /* . and .. links */
		tdvp->typedata.directory.attr.nlinks--;
	}
	return err;
}

static int tmpfs_open(struct vnode** vnode, int flags, const struct cred* cred) {
	(void)flags;
	(void)cred;
	(void)vnode;
	return 0;
}

static int tmpfs_close(struct vnode* vnode, int flags, const struct cred* cred) {
	(void)vnode;
	(void)flags;
	(void)cred;
	return 0;
}

static int tmpfs_read(struct vnode* vnode, void* buf, size_t count, off_t off, int flags, size_t* out_readc, const struct cred* cred) {
	(void)flags;
	(void)cred;
	if (off < 0 || !out_readc)
		return -EINVAL;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	struct tmpfs_block* block = tnode->typedata.data;

	size_t size = block->attr.size;
	if ((size_t)off >= size) {
		*out_readc = 0;
		return 0;
	}

	size_t avail = size - (size_t)off;
	if (count > avail)
		count = avail;

	memcpy(buf, block->data.file.data + off, count);
	*out_readc = count;
	return 0;
}

static int tmpfs_write(struct vnode* vnode, const void* buf, size_t count, off_t off, int flags, size_t* out_wcount, const struct cred* cred) {
	(void)flags;
	(void)cred;
	if (off < 0 || !out_wcount)
		return -EINVAL;
	if (count == 0) {
		*out_wcount = 0;
		return 0;
	}

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	size_t end;
	if (__builtin_add_overflow(off, count, &end))
		return -EFBIG;

	struct tmpfs_block* block = tnode->typedata.data;
	if (end > block->data.file.cap) {
		size_t new_cap = (end + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);
		u8* new_data = vmap(NULL, new_cap, PGPROT_READ | PGPROT_WRITE, VMM_ALLOC, NULL);
		if (IS_PTR_ERR(new_data))
			return PTR_ERR(new_data);
		if (block->data.file.data) {
			memcpy(new_data, block->data.file.data, block->data.file.cap);
			bug(vunmap(block->data.file.data, block->data.file.cap, 0, NULL) != 0);
		}
		block->data.file.data = new_data;
		block->data.file.cap = new_cap;
	}

	if ((size_t)off > block->attr.size)
		memset(block->data.file.data + block->attr.size, 0, (size_t)off - block->attr.size);
	memcpy(block->data.file.data + off, buf, count);
	if (end > block->attr.size)
		block->attr.size = end;

	*out_wcount = count;
	return 0;
}

static int tmpfs_lookup(struct vnode* ref, const char* name, struct vnode** out, const struct cred* cred) {
	(void)cred;
	if (!name || !out)
		return -EINVAL;
	if (strlen(name) == 0)
		return -ENOENT;
	if (strlen(name) > NAME_MAX)
		return -ENAMETOOLONG;
	if (ref->type != VTYPE_DIR)
		return -ENOTDIR;

	struct tmpfs_node* tnode = container_of(ref, struct tmpfs_node, vnode);
	struct tmpfs_node* lookup;
	int ret = hashtable_search(tnode->typedata.directory.children, name, strlen(name), &lookup);
	if (ret)
		return -ENOENT;

	VOP_HOLD(&lookup->vnode);
	VOP_LOCK(&lookup->vnode);
	*out = &lookup->vnode;
	return 0;
}

static int tmpfs_sync(struct vnode* vnode) {
	(void)vnode;
	return 0;
}

static int tmpfs_getattr(struct vnode* vnode, struct vattr* attr, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	const struct vattr* tattr = (vnode->type == VTYPE_DIR) ? &tnode->typedata.directory.attr : &tnode->typedata.data->attr;
	memcpy(attr, tattr, sizeof(*attr));
	return 0;
}

static int tmpfs_setattr(struct vnode* vnode, const struct vattr* attr, int attrs, const struct cred* cred) {
	(void)cred;
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	struct vattr* tattr = (vnode->type == VTYPE_DIR) ? &tnode->typedata.directory.attr : &tnode->typedata.data->attr;
	if (attrs & VATTR_MODE)
		tattr->mode = attr->mode;
	if (attrs & VATTR_UID)
		tattr->uid = attr->uid;
	if (attrs & VATTR_GID)
		tattr->gid = attr->gid;
	if (attrs & VATTR_ATIME)
		tattr->atime = attr->atime;
	if (attrs & VATTR_MTIME)
		tattr->mtime = attr->mtime;
	if (attrs & VATTR_CTIME)
		tattr->ctime = attr->ctime;
	return 0;
}

static int tmpfs_link(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred* cred) {
	(void)cred;
	if (!dvp || !svp || !name || strlen(name) == 0)
		return -EINVAL;
	if (dvp->belongs_to != svp->belongs_to)
		return -EXDEV;
	if (dvp->type != VTYPE_DIR)
		return -ENOTDIR;
	if (svp->type == VTYPE_DIR)
		return -EPERM;

	struct tmpfs_node* tnode = new_node(dvp->belongs_to, VTYPE_REGULAR, 0);
	if (!tnode)
		return -ENOMEM;

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	int err = hashtable_insert(tdvp->typedata.directory.children, name, strlen(name), &tnode);
	if (err) {
		destroy_node(tnode);
		return err;
	}

	struct tmpfs_node* tsvp = container_of(svp, struct tmpfs_node, vnode);
	struct tmpfs_block* block = tsvp->typedata.data;
	block->attr.nlinks++;
	tnode->typedata.data = block;

	return 0;
}

static int tmpfs_symlink(struct vnode* dvp, const char* name, const char* target, const struct vattr* attr, const struct cred* cred) {
	(void)cred;
	if (!name || !target || strlen(name) == 0 || strlen(target) == 0)
		return -EINVAL;
	if (strlen(target) > PATHNAME_MAX)
		return -ENAMETOOLONG;
	if (dvp->type != VTYPE_DIR)
		return -ENOTDIR;

	struct tmpfs_node* tnode = new_node(dvp->belongs_to, VTYPE_LINK, 0);
	if (!tnode)
		return -ENOMEM;
	const struct vattr tattr = tmpfs_sanitize_attr(container_of(dvp->belongs_to, struct tmpfs_mount, mount), VTYPE_LINK, attr);
	int err = init_regular_or_symlink(tnode, &tattr);
	if (err) {
		destroy_node(tnode);
		return err;
	}

	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);
	err = hashtable_insert(tdvp->typedata.directory.children, name, strlen(name), &tnode);
	if (err) {
		destroy_regular_or_symlink(tnode);
		destroy_node(tnode);
		return err;
	}

	strlcpy(tnode->typedata.data->data.symlink, target, sizeof(tnode->typedata.data->data.symlink));
	return 0;
}

static int tmpfs_unlink(struct vnode* dvp, struct vnode* child, const char* name, const struct cred* cred) {
	(void)cred;
	if (!dvp || !child || !name || strlen(name) == 0)
		return -EINVAL;
	if (dvp->type != VTYPE_DIR)
		return -ENOTDIR;
	if (child->type == VTYPE_DIR)
		return -EISDIR;

	struct tmpfs_node* tchild = container_of(child, struct tmpfs_node, vnode);
	struct tmpfs_node* tdvp = container_of(dvp, struct tmpfs_node, vnode);

	/* Check to make sure the name resolves to the same child */
	struct tmpfs_node* _tmp;
	bug(hashtable_search(tdvp->typedata.directory.children, name, strlen(name), &_tmp) != 0);
	bug(_tmp != tchild);

	/* Now just remove the reference to the child */
	int err = hashtable_remove(tdvp->typedata.directory.children, name, strlen(name));
	if (err == 0) {
		struct tmpfs_block* block = tchild->typedata.data;
		block->attr.nlinks--;
	}
	return err;
}

static int tmpfs_readlink(struct vnode* vnode, char** out, const struct cred* cred) {
	(void)cred;
	if (vnode->type != VTYPE_LINK)
		return -EINVAL;

	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	char* _out = kstrdup(tnode->typedata.data->data.symlink, MM_ZONE_NORMAL);
	if (!_out)
		return -ENOMEM;

	*out = _out;
	return 0;
}

static void tmpfs_freelink(char* link) {
	kfree(link);
}

struct tmpfs_dent_ctx {
	struct dirent* buf;
	const size_t count;
	size_t rcount;
	const off_t off;
	off_t npos;
};

static foreach_iteration_desicion_t tmpfs_foreach_getdents(struct hashtable* table, struct hashtable_node* node, void* ctx) {
	(void)table;
	struct tmpfs_dent_ctx* data = ctx;
	if (data->npos < data->off) {
		data->npos++;
		return FOREACH_ITERATION_DESICION_CONTINUE;
	}
	if (data->rcount == data->count)
		return FOREACH_ITERATION_DESICION_BREAK;

	struct dirent* entry = &data->buf[data->rcount];
	struct tmpfs_node* tnode;
	memcpy(&tnode, node->value, sizeof(struct tmpfs_node*));

	entry->ino = (tnode->vnode.type == VTYPE_DIR) ? tnode->typedata.directory.attr.inode : tnode->typedata.data->attr.inode;
	entry->off = data->npos;
	entry->reclen = sizeof(*entry);
	strlcpy(entry->name, node->key, sizeof(entry->name));
	entry->type = vfs_posix_type(tnode->vnode.type);
	data->rcount++;

	return FOREACH_ITERATION_DESICION_CONTINUE;
}

static int tmpfs_getdents(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount) {
	if (off < 0)
		return -EINVAL;
	if (dir->type != VTYPE_DIR)
		return -ENOTDIR;

	if (count) {
		if (!buf)
			return -EINVAL;
		struct tmpfs_dent_ctx ctx = { .buf = buf, .count = count, .rcount = 0, .off = off, .npos = 0 };
		struct tmpfs_node* tdir = container_of(dir, struct tmpfs_node, vnode);
		hashtable_for_each_entry(tdir->typedata.directory.children, tmpfs_foreach_getdents, &ctx);
		*out_rcount = ctx.rcount;
	} else {
		*out_rcount = 0;
	}

	return 0;
}

static int tmpfs_lock(struct vnode* vnode) {
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	if (vnode->type == VTYPE_REGULAR)
		mutex_acquire(&tnode->typedata.data->mtx);
	else
		mutex_acquire(&vnode->mtx);
	return 0;
}

static int tmpfs_unlock(struct vnode* vnode) {
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	if (vnode->type == VTYPE_REGULAR)
		mutex_release(&tnode->typedata.data->mtx);
	else
		mutex_release(&vnode->mtx);
	return 0;
}

static void tmpfs_inactive(struct vnode* vnode) {
	struct tmpfs_node* tnode = container_of(vnode, struct tmpfs_node, vnode);
	struct vattr* attr = (vnode->type == VTYPE_DIR) ? &tnode->typedata.directory.attr : &tnode->typedata.data->attr;
	if (attr->nlinks == 0) {
		if (vnode->type == VTYPE_DIR)
			destroy_directory(tnode);
		else
			destroy_regular_or_symlink(tnode);
		destroy_node(tnode);
	}
}

static const struct vnode_ops tmpfs_node_ops = {
	.create = tmpfs_create,
	.mkdir = tmpfs_mkdir,
	.rmdir = tmpfs_rmdir,
	.open = tmpfs_open,
	.close = tmpfs_close,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.lookup = tmpfs_lookup,
	.sync = tmpfs_sync,
	.getattr = tmpfs_getattr,
	.setattr = tmpfs_setattr,
	.link = tmpfs_link,
	.symlink = tmpfs_symlink,
	.unlink = tmpfs_unlink,
	.readlink = tmpfs_readlink,
	.freelink = tmpfs_freelink,
	.getdents = tmpfs_getdents,
	.lock = tmpfs_lock,
	.unlock = tmpfs_unlock,
	.inactive = tmpfs_inactive
};

static int tmpfs_mnt_mount(struct vnode* backing, void* data, struct mount** out_mount) {
	(void)backing;
	(void)data;

	struct tmpfs_mount* tmpfs_mount = kzalloc(sizeof(*tmpfs_mount), MM_ZONE_NORMAL);
	if (!tmpfs_mount)
		return -ENOMEM;
	struct tmpfs_node* tmproot = new_node(&tmpfs_mount->mount, VTYPE_DIR, VFLAG_ROOT);
	if (!tmproot) {
		kfree(tmpfs_mount);
		return -ENOMEM;
	}

	tmpfs_mount->mount.root = &tmproot->vnode;
	const struct vattr _attr = {
		.uid = current_cred()->uid,
		.gid = current_cred()->gid,
		.mode = 0
	};
	const struct vattr attr = tmpfs_sanitize_attr(tmpfs_mount, VTYPE_DIR, &_attr);
	int err = init_directory(tmproot, &attr);
	if (err) {
		destroy_node(tmproot);
		kfree(tmpfs_mount);
		return err;
	}
	err = hashtable_insert(tmproot->typedata.directory.children, ".", 1, &tmproot);
	if (err)
		goto cleanup_directory;
	err = hashtable_insert(tmproot->typedata.directory.children, "..", 2, &tmproot);
	if (err)
		goto cleanup_directory;

	tmpfs_mount->id = atomic_fetch_add(&id, 1);
	*out_mount = &tmpfs_mount->mount;
	return 0;
cleanup_directory:
	destroy_directory(tmproot);
	destroy_node(tmproot);
	kfree(tmpfs_mount);
	return err;
}

static int tmpfs_mnt_unmount(struct mount* mount) {
	(void)mount;
	return -ENOSYS;
}

static int tmpfs_mnt_sync(struct mount* mount) {
	(void)mount;
	return 0;
}

static int tmpfs_mnt_statfs(struct mount* mount, struct fsattr* attr) {
	size_t page_count, free_pages;
	mm_get_free_pages(&page_count, &free_pages);
	struct tmpfs_mount* mnt = container_of(mount, struct tmpfs_mount, mount);
	*attr = (struct fsattr){
		.io_size = PAGE_SIZE,
		.block_size = PAGE_SIZE,
		.block_count = page_count,
		.free_blocks = free_pages,
		.free_blocks_unprivileged = free_pages,
		.inode_count = SIZE_MAX,
		.free_inode_count = SIZE_MAX,
		.free_inode_count_unprivileged = SIZE_MAX,
		.fsid = mnt->id,
		.flags = 0,
		.max_name_size = NAME_MAX
	};
	return 0;
}

static const struct filesystem_type __filesystem_type tmpfs_type = {
	.name = "tmpfs",
	.mount = tmpfs_mnt_mount,
	.unmount = tmpfs_mnt_unmount,
	.sync = tmpfs_mnt_sync,
	.statfs = tmpfs_mnt_statfs
};

static void tmpfs_init(void) {
	tmpnode_cache = slab_cache_create(sizeof(struct tmpfs_node), alignof(struct tmpfs_node), MM_ZONE_NORMAL, NULL, NULL);
	if (!tmpnode_cache)
		out_of_memory();
	int err = vfs_register(&tmpfs_type);
	if (unlikely(err))
		printk(PRINTK_WARN "tmpfs: Failed to register type, %d\n", err);
}

INIT_TASK_DECLARE(heap_init_task, vfs_init_task);
INIT_TASK_DEFINE(tmpfs_init_task, INIT_TASK_SCOPE_BSP, tmpfs_init, &heap_init_task, &vfs_init_task);
