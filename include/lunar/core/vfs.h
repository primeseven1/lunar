#pragma once

#include <lunar/core/mutex.h>

typedef int uid_t;
typedef int gid_t;

enum vfs_node_types {
	VFS_NODE_FILE,
	VFS_NODE_DIR
};

struct vfs_attr {
	int type;
	size_t size, fs_block_size, blocks_used;
	struct timespec atime, mtime, ctime;
	uid_t uid;
	gid_t gid;
};

struct vfs_node;

struct vfs_node_ops {
	int (*open)(struct vfs_node*, int flags);
	int (*close)(struct vfs_node*, int flags);
	ssize_t (*read)(struct vfs_node*, void* buf, size_t size, u64 off, int flags);
	ssize_t (*write)(struct vfs_node*, const void* buf, size_t size, u64 off, int flags);
	int (*lookup)(struct vfs_node*, const char* name, struct vfs_node**);
	int (*getattr)(struct vfs_node*, struct vfs_attr*);
	int (*setattr)(struct vfs_node*, const struct vfs_attr*);
};

struct vfs_node {
	struct vfs_node_ops* ops;
	int type;
	struct vfs_node* parent;
	mutex_t lock;
	atomic(int) refcount;
};

struct vfs_superblock;

struct vfs_superblock_ops {
	int (*unmount)(struct vfs_superblock*);
	int (*sync)(struct vfs_superblock*);
};

struct vfs_superblock {
	struct vfs_superblock_ops* ops;
	struct vfs_node* root;
	struct vfs_superblock* next;
};

struct filesystem_type {
	const char* name;
	int (*mount)(struct vfs_node* backing, struct vfs_superblock**);
	void (*kill_sb)(struct vfs_superblock*);
};

int vfs_open(struct vfs_node* n, int flags);
int vfs_close(struct vfs_node* n, int flags);
ssize_t vfs_read(struct vfs_node* n, void* buf, size_t size, u64 off, int flags);
ssize_t vfs_write(struct vfs_node* n, const void* buf, size_t size, u64 off, int flags);
int vfs_getattr(struct vfs_node* n, struct vfs_attr* attr);
int vfs_setattr(struct vfs_node* n, const struct vfs_attr* attr);
int vfs_lookup(struct vfs_node* dir, const char* name, struct vfs_node** out);
int vfs_register(struct filesystem_type* type);

void vfs_init(void);
