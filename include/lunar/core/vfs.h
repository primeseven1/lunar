#pragma once

#include <lunar/core/mutex.h>
#include <lunar/core/cred.h>
#include <lunar/core/abi.h>
#include <lunar/mm/heap.h>

#define PATHNAME_MAX 4096
#define NAME_MAX 255

enum vattr_opt_masks {
	VATTR_MODE = (1 << 0),
	VATTR_UID = (1 << 1),
	VATTR_GID = (1 << 2),
	VATTR_ATIME = (1 << 3),
	VATTR_MTIME = (1 << 4),
	VATTR_CTIME = (1 << 5)
};

enum vattr_mode_masks {
	VATTR_MODE_OTHERS_EXECUTE = 00001,
	VATTR_MODE_OTHERS_SEARCH = VATTR_MODE_OTHERS_EXECUTE,
	VATTR_MODE_OTHERS_WRITE = 00002,
	VATTR_MODE_OTHERS_READ = 00004,
	VATTR_MODE_OTHERS_ALL = 00007,
	VATTR_MODE_GROUP_EXECUTE = 00010,
	VATTR_MODE_GROUP_SEARCH = VATTR_MODE_GROUP_EXECUTE,
	VATTR_MODE_GROUP_WRITE = 00020,
	VATTR_MODE_GROUP_READ = 00040,
	VATTR_MODE_GROUP_ALL = 00070,
	VATTR_MODE_USER_EXECUTE = 00100,
	VATTR_MODE_USER_SEARCH = VATTR_MODE_USER_EXECUTE,
	VATTR_MODE_USER_WRITE = 00200,
	VATTR_MODE_USER_READ = 00400,
	VATTR_MODE_USER_ALL = 00700,
	VATTR_MODE_STICKY = 01000,
	VATTR_MODE_SGID = 02000,
	VATTR_MODE_SUID = 04000
};

struct vattr {
	mode_t mode;
	uid_t uid;
	gid_t gid;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	blkcnt_t blocks_used;
};

struct vnode;
struct mount;
struct filesystem_type;

enum vnode_flags {
	VNODE_FLAG_ROOT = (1 << 0)
};

/* More types will be added later */
enum vnode_types {
	VNODE_TYPE_MIN,
	VNODE_TYPE_REGULAR = VNODE_TYPE_MIN,
	VNODE_TYPE_DIR,
	VNODE_TYPE_MAX = VNODE_TYPE_DIR
};

enum vnode_lookup_flags {
	VNODE_LOOKUP_FLAG_PARENT = (1 << 0)
};

struct vnode_ops {
	int (*open)(struct vnode*, int flags, const struct cred*); /* Does whatever */
	int (*close)(struct vnode*, int flags, const struct cred*); /* Does more whatever */
	ssize_t (*read)(struct vnode*, void* buf, size_t size, off_t off, int flags, const struct cred*); /* Read from a vnode and store it in buf */
	ssize_t (*write)(struct vnode*, const void* buf, size_t size, off_t off, int flags, const struct cred*); /* Write to a vnode from buf */
	int (*lookup)(struct vnode* dir, const char* name, int flags, struct vnode** out, const struct cred*); /* Lookup a vnode by name */
	int (*create)(struct vnode* dir, const char* name, mode_t mode, int type, struct vnode** out, const struct cred*);
	int (*getattr)(struct vnode*, struct vattr*, const struct cred*);
	int (*setattr)(struct vnode*, struct vattr*, int attrs, const struct cred*);
	void (*release)(struct vnode*); /* Free a vnode */
};

struct vnode {
	int type;
	const struct vnode_ops* ops;
	struct mount* mount; /* What filesystem this vnode belongs to */
	int flags;
	atomic(int) refcount;
	void* fs_priv;
	union {
		struct mount* mounted_here; /* type is VNODE_TYPE_DIR, and is a mount point */
	};
	mutex_t lock;
};

static inline void vnode_ref(struct vnode* node) {
	atomic_add_fetch(&node->refcount, 1);
}

static inline void vnode_unref(struct vnode* node) {
	if (atomic_sub_fetch(&node->refcount, 1) == 0) {
		if (node->ops && node->ops->release)
			node->ops->release(node);
	}
}

struct mount {
	const struct filesystem_type* type;
	struct vnode* root;
	void* fs_private;
	struct vnode* parent;
};

struct filesystem_type {
	const char* name;
	int (*mount)(struct mount*, struct vnode* backing, void* data);
	int (*unmount)(struct mount*);
};
#define __filesystem_type __attribute__((section(".fstypes"), aligned(8)))

int vfs_register(const struct filesystem_type* type);

/**
 * @brief Mount a file system
 * 
 * @param mp The mount point
 * @param fs_name The name of the underlying file system
 * @param backing The backing device
 * @param data File system specific
 *
 * @return -errno on failure
 */
int vfs_mount(const char* mp, const char* fs_name, struct vnode* backing, void* data);

/**
 * @brief Unmount a file system
 * @param mp The mount point
 * @return -errno on failure
 */
int vfs_unmount(const char* mp);

/**
 * @brief Calls the file system ops for opening a vnode
 *
 * @param node The node to open
 * @param flags The flags for opening
 *
 * @return -errno on failure
 */
int vfs_open(struct vnode* node, int flags);

/**
 * @brief Closes a vnode
 *
 * @param node The node to close
 * @param flags Flags for closing the node
 *
 * @return -errno on failure
 */
int vfs_close(struct vnode* node, int flags);

/**
 * @brief Read from a vnode
 *
 * @param node The node to read from
 * @param buf The buffer to store the data in to
 * @param size The size of the buffer
 * @param off The offset into the file to read
 * @param flags Additional flags
 *
 * @return The bytes read, or -errno on failure
 */
ssize_t vfs_read(struct vnode* node, void* buf, size_t size, off_t off, int flags);

/**
 * @brief Write to a vnode
 *
 * @param node The node to write to
 * @param buf The data to write
 * @param size The number of bytes to write
 * @param off The offset of the file
 * @param flags Additional flags
 *
 * @return The number of bytes written, or -errno on failure
 */
ssize_t vfs_write(struct vnode* node, const void* buf, size_t size, u64 off, int flags);

/**
 * @brief Look up a vnode by name
 *
 * @param dir The directory to start searching from, can be NULL to start from root
 * @param path The path to the file/directory
 * @param lastcomp The last component of the path
 * @param flags Lookup flags (unused)
 * @param out Where to store the vnode
 *
 * @return -errno on failure
 */
int vfs_lookup(struct vnode* dir, const char* path, const char* lastcomp, int flags, struct vnode** out);

/**
 * @brief Create a new file/directory
 *
 * @param dir The parent directory
 * @param name The name of the file/directory
 * @param mode Access permissions, unused for now
 * @param type The type of the vnode
 * @param out Where the returned vnode should be stored, can be NULL
 *
 * @return -errno on failure
 */
int vfs_create(struct vnode* dir, const char* name, mode_t mode, int type, struct vnode** out);

void vfs_init(void);
