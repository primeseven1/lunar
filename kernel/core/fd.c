#include <lunar/fd.h>
#include <lunar/slab.h>
#include <lunar/init.h>
#include <lunar/proc.h>

static struct slab_cache* file_cache = NULL;

static void destroy_file(struct file* file) {
	if (file->vnode) {
		vfs_close(file->vnode, 0);
		VOP_RELEASE(file->vnode);
	}

	slab_cache_free(file_cache, file);
}

#define FILE_HOLD(f) atomic_add_fetch(&(f)->refcnt, 1)
#define FILE_RELEASE(f) \
	do { \
		if (atomic_sub_fetch(&(f)->refcnt, 1) == 0) \
			destroy_file(f); \
	} while (0)

struct fd* fd_create_table(void) {
	return kcalloc(FD_TABLE_ENTRY_COUNT, sizeof(struct fd), MM_ZONE_NORMAL);
}

void fd_destroy_table(struct fd* table) {
	for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
		if (table[i].file)
			FILE_RELEASE(table[i].file);
	}
	kfree(table);
}

static struct file* alloc_file(void) {
	struct file* ret = slab_cache_alloc(file_cache);
	if (ret) {
		ret->vnode = NULL;
		ret->offset = 0;
		ret->flags = 0;
		mutex_init(&ret->mtx);
		atomic_store(&ret->refcnt, 1);
	}
	return ret;
}

struct file* fd_get(int fd) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return NULL;

	struct proc* proc = current_proc();
	mutex_acquire(&proc->fd_mtx);
	struct file* file = proc->fd_table[fd].file;
	if (file)
		FILE_HOLD(file);
	mutex_release(&proc->fd_mtx);

	if (file)
		mutex_acquire(&file->mtx);
	return file;
}

void fd_release(struct file* file) {
	if (file) {
		mutex_release(&file->mtx);
		FILE_RELEASE(file);
	}
}

static int find_free_fd(struct proc* proc) {
	int fd = -1;
	for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
		if (!proc->fd_table[i].file) {
			fd = i;
			break;
		}
	}
	return fd;
}

int fd_new(int flags, struct file** out_file, int* out_fd) {
	struct file* file = alloc_file();
	if (!file)
		return -ENOMEM;

	int err = 0;

	struct proc* proc = current_proc();
	mutex_acquire(&proc->fd_mtx);

	int fd = find_free_fd(proc);
	if (fd >= 0)
		proc->fd_table[fd] = (struct fd){ .file = file, .flags = flags };
	else
		err = -EMFILE;

	mutex_release(&proc->fd_mtx);

	if (err == 0) {
		*out_file = file;
		*out_fd = fd;
	} else {
		destroy_file(file);
	}
	return err;
}

int fd_close(int fd) {
	if (fd < 0 || fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	struct proc* proc = current_proc();
	mutex_acquire(&proc->fd_mtx);

	struct file* file = proc->fd_table[fd].file;
	if (file)
		proc->fd_table[fd] = (struct fd){ .file = NULL, .flags = 0 };

	mutex_release(&proc->fd_mtx);
	if (file)
		FILE_RELEASE(file);
	else
		return -EBADF;
	return 0;
}

void fd_clone(struct fd* table) {
	struct proc* proc = current_proc();
	mutex_acquire(&proc->fd_mtx);

	for (int i = 0; i < FD_TABLE_ENTRY_COUNT; i++) {
		if (proc->fd_table[i].file) {
			table[i] = proc->fd_table[i];
			FILE_HOLD(table[i].file);
		}
	}

	mutex_release(&proc->fd_mtx);
}

int fd_dup(int old_fd, int new_fd, int flags, int* out_fd) {
	/* A negative newfd will be ignored */
	if (old_fd < 0 || old_fd >= FD_TABLE_ENTRY_COUNT || new_fd >= FD_TABLE_ENTRY_COUNT)
		return -EBADF;

	struct proc* proc = current_proc();
	struct file* old = NULL;
	int err = 0;

	mutex_acquire(&proc->fd_mtx);

	struct file* file = proc->fd_table[old_fd].file;
	if (!file) {
		err = -EBADF;
		goto out;
	}

	if (new_fd >= 0) {
		if (new_fd == old_fd) {
			*out_fd = new_fd;
			goto out;
		}

		old = proc->fd_table[new_fd].file;
		proc->fd_table[new_fd] = (struct fd){ .file = file, .flags = flags };
		*out_fd = new_fd;
	} else {
		int fd = find_free_fd(proc);
		if (fd < 0) {
			err = -EMFILE;
			goto out;
		}
		proc->fd_table[fd] = (struct fd){ .file = file, .flags = flags };
		*out_fd = fd;
	}

	FILE_HOLD(file);
out:
	mutex_release(&proc->fd_mtx);
	if (old)
		FILE_RELEASE(old);
	return err;
}

static void fd_init(void) {
	file_cache = slab_cache_create(sizeof(struct file), alignof(struct file), MM_ZONE_NORMAL, NULL, NULL);
	if (!file_cache)
		out_of_memory();
}

INIT_TASK_DECLARE(zones_init_task);
INIT_TASK_DEFINE(fd_init_task, INIT_TASK_SCOPE_BSP, fd_init, &zones_init_task);
