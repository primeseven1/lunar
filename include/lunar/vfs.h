#pragma once

#include <lunar/time.h>
#include <lunar/mutex.h>
#include <lunar/cred.h>
#include <arch/posix.h>
#include <arch/asm/errno.h>

#define PATHNAME_MAX 4095
#define MAX_LINK_DEPTH 24

#define VATTR_MODE_OTHERS_EXECUTE 00001
#define VATTR_MODE_OTHERS_SEARCH VATTR_MODE_OTHERS_EXECUTE
#define VATTR_MODE_OTHERS_WRITE 00002
#define VATTR_MODE_OTHERS_READ 00004
#define VATTR_MODE_OTHERS_ALL 00007
#define VATTR_MODE_GROUP_EXECUTE 00010
#define VATTR_MODE_GROUP_SEARCH VATTR_MODE_GROUP_EXECUTE
#define VATTR_MODE_GROUP_WRITE 00020
#define VATTR_MODE_GROUP_READ 00040
#define VATTR_MODE_GROUP_ALL 00070
#define VATTR_MODE_USER_EXECUTE 00100
#define VATTR_MODE_USER_SEARCH VATTR_MODE_USER_EXECUTE
#define VATTR_MODE_USER_WRITE 00200
#define VATTR_MODE_USER_READ 00400
#define VATTR_MODE_USER_ALL 00700
#define VATTR_MODE_STICKY 01000
#define VATTR_MODE_SGID 02000
#define VATTR_MODE_SUID 04000

#define VATTR_MODE (1 << 0)
#define VATTR_UID (1 << 1)
#define VATTR_GID (1 << 2)
#define VATTR_ATIME (1 << 3)
#define VATTR_MTIME (1 << 4)
#define VATTR_CTIME (1 << 5)
#define VATTR_SIZE (1 << 6)
#define VATTR_ALL (VATTR_MODE | VATTR_UID | VATTR_GID | VATTR_ATIME | VATTR_MTIME | VATTR_CTIME | VATTR_SIZE)

#define VFLAG_ROOT (1 << 0)

#define VFS_LOOKUP_LINK_DEPTH_MASK 0xFF
#define VFS_LOOKUP_PARENT (1 << 8)
#define VFS_LOOKUP_NOFOLLOW (1 << 9)

struct vnode;
struct mount;

enum vtype {
	VTYPE_REGULAR,
	VTYPE_DIR,
	VTYPE_CHDEV,
	VTYPE_BLKDEV,
	VTYPE_FIFO,
	VTYPE_LINK,
	VTYPE_SOCKET
};

struct fsattr {
	size_t io_size;
	size_t block_size, block_count;
	size_t free_blocks, free_blocks_unprivileged;
	size_t inode_count, free_inode_count, free_inode_count_unprivileged;
	unsigned long fsid;
	unsigned long flags;
	size_t max_name_size;
};

struct filesystem_type {
	const char* name;
	int (*mount)(struct vnode* backing, void* data, struct mount** out_mnt);
	int (*unmount)(struct mount*);
	int (*sync)(struct mount*);
	int (*statfs)(struct mount*, struct fsattr* out);
};
#define __filesystem_type __attribute__((section(".fstypes"), aligned(8)))

struct mount {
	const struct filesystem_type* fs_type;
	struct vnode* root; /* Root of the file system */
	struct vnode* covered; /* The vnode in the parent file system this is mounted on */
	struct list_node link; /* For global mount point list */
	atomic(long) refcnt; /* Number of open file handles/number of mount points */
};

struct vattr {
	enum vtype type;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	int fsid;
	ino_t inode;
	int nlinks;
	size_t size, fsblocksize;
	struct timespec atime, mtime, ctime;
	int rdevmajor, rdevminor;
	int devmajor, devminor;
	size_t blocksused;
};

struct vnode_ops {
	int (*create)(struct vnode* dvp, const char*, const struct vattr*, struct vnode** out, const struct cred*);
	int (*mkdir)(struct vnode* dvp, const char*, const struct vattr*, struct vnode** out, const struct cred*);
	int (*rmdir)(struct vnode* dvp, struct vnode*, const char* name, const struct cred*);
	int (*open)(struct vnode**, int, const struct cred*);
	int (*close)(struct vnode*, int, const struct cred*);
	int (*read)(struct vnode*, void* buf, size_t count, off_t, int flags, size_t* out_rcount, const struct cred*);
	int (*write)(struct vnode*, const void* buf, size_t count, off_t, int flags, size_t* out_wcount, const struct cred*);
	int (*lookup)(struct vnode* ref, const char*, struct vnode** out, const struct cred*);
	int (*sync)(struct vnode*); /* Sync a vnode to disk */
	int (*getattr)(struct vnode*, struct vattr* out, const struct cred*);
	int (*setattr)(struct vnode*, const struct vattr*, int aflags, const struct cred*);
	int (*link)(struct vnode* dvp, struct vnode* svp, const char* name, const struct cred*);
	int (*symlink)(struct vnode* dvp, const char* name, const char* target, const struct vattr*, const struct cred*);
	int (*unlink)(struct vnode* dvp, struct vnode* child, const char* name, const struct cred*);
	int (*readlink)(struct vnode*, char** out, const struct cred*);
	void (*freelink)(char* str);
	int (*getdents)(struct vnode* dir, struct dirent* buf, size_t count, off_t off, size_t* out_rcount);
	int (*lock)(struct vnode*);
	int (*unlock)(struct vnode*);
	void (*inactive)(struct vnode*);
};

struct vnode {
	enum vtype type;
	const struct vnode_ops* ops;
	int flags;
	struct mount* belongs_to;
	union {
		struct mount* mounted_here; /* VTYPE_DIR */
	} un;
	atomic(long) refcnt;
	mutex_t mtx;
};

#define VOP_CREATE(dvp, name, vap, out, cr) (dvp)->ops->create(dvp, name, vap, out, cr)
#define VOP_MKDIR(dvp, name, vap, out, cr) (dvp)->ops->mkdir(dvp, name, vap, out, cr)
#define VOP_RMDIR(dvp, v, name, cr) (dvp)->ops->rmdir(dvp, v, name, cr)
#define VOP_OPEN(vpp, flags, cr) (*(vpp))->ops->open(vpp, flags, cr)
#define VOP_CLOSE(v, flags, cr) (v)->ops->close(v, flags, cr)
#define VOP_READ(v, buf, count, off, flags, rc, cr) (v)->ops->read(v, buf, count, off, flags, rc, cr)
#define VOP_WRITE(v, buf, count, off, flags, wc, cr) (v)->ops->write(v, buf, count, off, flags, wc, cr)
#define VOP_LOOKUP(ref, name, out, cr) (ref)->ops->lookup(ref, name, out, cr)
#define VOP_SYNC(v) (v)->ops->sync(v)
#define VOP_GETATTR(v, vap, cr) (v)->ops->getattr(v, vap, cr)
#define VOP_SETATTR(v, vap, flags, cr) (v)->ops->setattr(v, vap, flags, cr)
#define VOP_LINK(dvp, svp, name, cr) (dvp)->ops->link(dvp, svp, name, cr)
#define VOP_SYMLINK(dvp, name, target, attr, cr) (dvp)->ops->symlink(dvp, name, target, attr, cr)
#define VOP_UNLINK(dvp, child, name, cr) (dvp)->ops->unlink(dvp, child, name, cr)
#define VOP_READLINK(v, out, cr) (v)->ops->readlink(v, out, cr)
#define VOP_FREELINK(v, str) (v)->ops->freelink(str)
#define VOP_GETDENTS(v, buf, count, off, rc) (v)->ops->getdents(v, buf, count, off, rc)
#define VOP_LOCK(v) ((v)->ops->lock ? (v)->ops->lock(v) : (mutex_acquire(&(v)->mtx), 0))
#define VOP_UNLOCK(v) ((v)->ops->unlock ? (v)->ops->unlock(v) : (mutex_release(&(v)->mtx), 0))
#define VOP_HOLD(v) atomic_add_fetch_explicit(&(v)->refcnt, 1, ATOMIC_RELAXED)
#define VOP_RELEASE(v) \
	do { \
		if (atomic_sub_fetch_explicit(&(v)->refcnt, 1, ATOMIC_ACQ_REL) == 0) \
			(v)->ops->inactive(v); \
	} while (0)
#define VOP_INIT(vn, t, o, f, m) \
	do { \
		(vn)->type = t; \
		(vn)->ops = o; \
		(vn)->flags = f; \
		(vn)->belongs_to = m; \
		__builtin_memset(&(vn)->un, 0, sizeof((vn)->un)); \
		atomic_store_explicit(&(vn)->refcnt, 1, ATOMIC_RELAXED); \
		mutex_init(&(vn)->mtx); \
	} while (0)

/**
 * @brief Register a file system type
 * @param fs_type File system type to register
 * @retval -EEXIST File system type by name already exists
 * @retval -ENOMEM Out of memory
 * @retval 0 Successful
 */
int vfs_register(const struct filesystem_type* fs_type);

/**
 * @brief Mount a file system
 *
 * @param fs_name File system name
 * @param ref Reference vnode, expected to be unlocked
 * @param mp Mount point
 * @param backing Backing vnode (eg. /dev/sda)
 *
 * @retval -ENODEV fs_name does not exist
 * @retval 0 Successful
 */
int vfs_mount(const char* fs_name, struct vnode* ref, const char* mp, struct vnode* backing);

/**
 * @brief Create a regular vnode
 *
 * @param[in] ref Reference for lookup
 * @param[in] path Path to the file
 * @param[in] attr File attributes
 * @param[out] out Where the created vnode is stored, optional
 *
 * @retval 0 Successful
 */ 
int vfs_create(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out);

/**
 * @brief Create a directory vnode
 *
 * @param[in] ref Reference for lookup
 * @param[in] path Path to where the directory will be made
 * @param[in] attr Directory attributes
 * @param[out] out Where the created vnode is stored, optional
 *
 * @retval 0 Successful
 * @retval -ENOENT Lookup failed
 * @retval -EEXIST Directory already exists
 */
int vfs_mkdir(struct vnode* ref, const char* path, const struct vattr* attr, struct vnode** out);

/**
 * @brief Remove a directory vnode
 *
 * @param ref Reference for lookup
 * @param path Path to the directory
 *
 * @retval 0 Successful
 * @retval -ENOTEMPTY Directory not empty
 * @retval -ENOENT Lookup failed
 * @retval -ENOTDIR Not a directory
 */
int vfs_rmdir(struct vnode* ref, const char* path);

/**
 * @brief Open a vnode
 *
 * @param[in] ref Reference for the path
 * @param[in] path Path to the file
 * @param[in] flags Open flags
 * @param[out] out Where to store the opened vnode
 *
 * @return Look at vfs_lookup() for details
 */
int vfs_open(struct vnode* ref, const char* path, int flags, struct vnode** out);

/**
 * @brief Close a vnode
 *
 * @param vnode The vnode to close
 * @param flags Close flags
 *
 * @return -errno on failure, 0 on success
 */
int vfs_close(struct vnode* vnode, int flags);

/**
 * @brief Read a file
 *
 * @param[in] buf Where to store the file data
 * @param[in] count The number of bytes to read
 * @param[in] off Offset into the file
 * @param[in] flags Read flags
 * @param[out] rcount Actual number of bytes read
 *
 * @retval 0 Successful
 */
int vfs_read(struct vnode* vnode, void* buf, size_t count, off_t off, int flags, size_t* rcount);

/**
 * @brief Write to a file
 *
 * @param vnode The vnode to write to
 * @param buf The buffer to write
 * @param count The number of bytes to write
 * @param off Offset into the file
 * @param flags flags
 * @param wcount Pointer to store the write count
 *
 * @retval 0 Successful
 */
int vfs_write(struct vnode* vnode, const void* buf, size_t count, off_t off, int flags, size_t* wcount);

/**
 * @brief Create a symbolic link or hardlink
 *
 * @param dref Destination path reference for lookup
 * @param dpath Destination path for existing target
 * @param lref Link reference for lookup
 * @param lpath Path to where the new link will be created
 * @param type Type of link (VTYPE_REGULAR for hardlink, VTYPE_LINK for symbolic link)
 * @param attr File attributes, ignored for hardlinks
 *
 * @retval -EPERM Trying to hardlink a directory
 * @retval 0 Successful
 */
int vfs_link(struct vnode* dref, const char* dpath, struct vnode* lref, const char* lpath, enum vtype type, const struct vattr* attr);

/**
 * @brief Unlink a file
 *
 * @param ref Reference for the lookup
 * @param path Path to the file
 *
 * @retval 0 Successful
 */
int vfs_unlink(struct vnode* ref, const char* path);

/**
 * @brief Resolve a file system path to a vnode
 *
 * @param[in] ref The reference vnode, expected to be unlocked
 * @param[in] path The path to look up
 * @param[out] lastcomp Pointer to a last component buffer when looking up a parent vnode, buffer must be NAME_MAX + 1 in size or bigger
 * @param flags[in] VFS_LOOKUP_* flags
 * @param out[out] Where the resolved vnode is stored
 *
 * @retval -EINVAL path is NULL, or out is NULL, or lastcomp is NULL and VFS_LOOKUP_PARENT is used
 * @retval -ENOENT Not found
 * @retval -ENOSYS File system driver does not implement the required functions
 * @retval -ENOTDIR Component in the middle of the path does not resolve to a directory vnode, or ref is not a directory
 * @retval -ENAMETOOLONG Path length is more than PATHNAME_MAX, or a component in the path is longer than NAME_MAX
 * @retval -ELOOP Too many symbolic links
 * @retval 0 Successful
 */
int vfs_lookup(struct vnode* ref, const char* path, char* lastcomp, int flags, struct vnode** out);

/**
 * @brief Mount the root file system
 */
void vfs_mount_root(void);

static inline unsigned char vfs_posix_type(enum vtype type) {
	switch (type) {
	case VTYPE_REGULAR:
		return DT_REG;
	case VTYPE_DIR:
		return DT_DIR;
	case VTYPE_CHDEV:
		return DT_CHR;
	case VTYPE_BLKDEV:
		return DT_BLK;
	case VTYPE_FIFO:
		return DT_FIFO;
	case VTYPE_LINK:
		return DT_LNK;
	case VTYPE_SOCKET:
		return DT_SOCK;
	}
	return DT_UNKNOWN;
}
