#pragma once

#include <lunar/vfs.h>
#include <lunar/slab.h>

#define FD_TABLE_ENTRY_COUNT 1024

struct file {
	struct vnode* vnode;
	off_t offset;
	int flags;
	mutex_t mtx;
	long __atomic refcnt;
};

struct fd {
	struct file* file;
	int flags;
};

/**
 * @brief Create a new fd table
 *
 * The table returned has FD_TABLE_ENTRY_COUNT entries.
 *
 * @return The table
 */
struct fd* fd_create_table(void);

/**
 * @brief Destroy a fd table
 * @param table The table to destroy
 */
void fd_destroy_table(struct fd* table);

/**
 * @brief Get a file in a process fd table
 *
 * Returns the file locked and referenced
 *
 * @param fd The file descriptor
 * @return NULL if there is no file associated with the FD
 */
struct file* fd_get(int fd);

/**
 * @brief Release a file in a fd table
 *
 * Unlocks and unref's the file
 *
 * @param file The file to release
 */
void fd_release(struct file* file);

/**
 * @brief Create a new fd
 *
 * @param[in] flags The file descriptor flags
 * @param[out] out_file Where to store the output file
 * @param[out] out_fd Where to store the fd
 *
 * @retval -ENOMEM Out of memory
 * @retval -EMFILE Too many file descriptors open
 * @retval 0 Successful
 */
int fd_new(int flags, struct file** out_file, int* out_fd);

/**
 * @brief Close a fd
 * @param fd The fd to close
 * @retval -EBADF No file associated with the fd
 * @retval 0 Successful
 */
int fd_close(int fd);

/**
 * @brief Clone the current process fd table into another fd table
 * @param table The table, this is expected to be allocated by fd_create_table().
 */
void fd_clone(struct fd* table);

/**
 * @brief Duplicate an open file descriptor
 *
 * @param old_fd The old file descriptor
 * @param new_fd The file descriptor to assocate old_fd with. Ignored if < 0.
 * @param flags The flags for the new file descriptor
 * @param out_fd Where the new file descriptor will be stored
 *
 * @retval -EBADF Bad old file descriptor, or the new file descriptor is >= FD_TABLE_ENTRY_COUNT
 * @retval -EMFILE Too many open file descriptors
 * @retval 0 Successful
 */
int fd_dup(int old_fd, int new_fd, int flags, int* out_fd);
