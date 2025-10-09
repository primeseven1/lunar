#pragma once

#include <lunar/core/mutex.h>
#include <lunar/core/cred.h>
#include <lunar/mm/heap.h>

enum vfs_node_types {
	VFS_NODE_FILE,
	VFS_NODE_DIR
};

#define VFS_LOOKUP_PARENT (1 << 0)

struct vfs_attr {
	int type;
	size_t size, fs_block_size, blocks_used;
	struct timespec atime, mtime, ctime;
	uid_t uid;
	gid_t gid;
};

struct vfs_node;

struct vfs_node_ops {
	int (*create)(struct vfs_node* dir, const char* name, int type, struct vfs_node** out, const struct cred*);
	int (*open)(struct vfs_node*, int flags, const struct cred*);
	int (*close)(struct vfs_node*, int flags, const struct cred*);
	ssize_t (*read)(struct vfs_node*, void* buf, size_t size, u64 off, int flags, const struct cred*);
	ssize_t (*write)(struct vfs_node*, const void* buf, size_t size, u64 off, int flags, const struct cred*);
	int (*lookup)(struct vfs_node*, const char* name, int flags, struct vfs_node**, const struct cred*);
	int (*getattr)(struct vfs_node*, struct vfs_attr*, const struct cred*);
	int (*setattr)(struct vfs_node*, const struct vfs_attr*, const struct cred*);
};

struct vfs_node {
	const struct vfs_node_ops* ops;
	int type;
	mutex_t lock;
	atomic(int) refcount;
	void* fs_priv;
	struct vfs_node* mp_parent; /* If this is a mount point, this is the parent directory */
};

struct vfs_superblock;

struct vfs_superblock_ops {
	int (*unmount)(struct vfs_superblock*, const struct cred*);
	int (*sync)(struct vfs_superblock*);
};

struct vfs_superblock {
	const struct vfs_superblock_ops* ops;
	struct vfs_node* root;
	struct vfs_superblock* next;
};

struct filesystem_type {
	const char* name;
	int (*mount)(struct vfs_node* backing, void* data, struct vfs_superblock**, const struct cred*);
	void (*kill_sb)(struct vfs_superblock*);
};

#define __filesystem_type __attribute__((section(".fstypes"), aligned(8)))

static inline void vfs_node_get(struct vfs_node* n) {
	atomic_add_fetch(&n->refcount, 1);
}

static inline void vfs_node_put(struct vfs_node* n) {
	if (atomic_sub_fetch(&n->refcount, 1) == 0)
		kfree(n);
}

int vfs_create(struct vfs_node* dir, const char* name, int type, struct vfs_node** out);
int vfs_open(struct vfs_node* n, int flags);
int vfs_close(struct vfs_node* n, int flags);
ssize_t vfs_read(struct vfs_node* n, void* buf, size_t size, u64 off, int flags);
ssize_t vfs_write(struct vfs_node* n, const void* buf, size_t size, u64 off, int flags);
int vfs_getattr(struct vfs_node* n, struct vfs_attr* attr);
int vfs_setattr(struct vfs_node* n, const struct vfs_attr* attr);
int vfs_lookup(struct vfs_node* dir, const char* name, struct vfs_node** out);

int vfs_register(const struct filesystem_type* type);
int vfs_mount(const char* fs_name, const char* mp, struct vfs_node* backing, void* data);
int vfs_unmount(const char* target_path);

void vfs_init(void);
