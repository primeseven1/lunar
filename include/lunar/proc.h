#pragma once

#include <lunar/cred.h>
#include <lunar/mm.h>
#include <lunar/vfs.h>
#include <lunar/mutex.h>
#include <lunar/sched.h>
#include <arch/posix.h>

#define PROC_THREAD_ATTACHED_REFCOUNT 1 /* How many references a process holds to a thread when attached */

struct proc {
	pid_t pid;
	struct cred cred;
	struct mm* mm_struct;
	struct {
		struct list_head list;
		atomic(unsigned int) count;
		spinlock_t lock;
	} threads;
	struct {
		struct vnode* cwd, *root;
		mutex_t mtx;
	} fs;
	atomic(unsigned long) refcnt;
};

/**
 * @brief Get the current process
 *
 * Does NOT increase the refcount
 *
 * @return A pointer to the current process
 */
static inline struct proc* current_proc(void) {
	return atomic_load(&current_thread()->proc);
}

/**
 * @brief Get a process by PID
 *
 * Process is returned with a ref, use PROC_RELEASE() to unref.
 *
 * @param[in] pid The PID of the process
 * @param[out] out Pointer to where the process will be stored
 *
 * @retval -ESRCH No process found
 * @retval 0 Successful
 */
int proc_get(pid_t pid, struct proc** out);

/**
 * @brief Create a process structure
 *
 * Pointer is returned with a ref, use PROC_RELEASE() to unref.
 *
 * @param[out] out Pointer to where the process will be stored
 *
 * @retval 0 Successful
 * @retval -ENOMEM Out of memory
 */
int proc_create(struct proc** out);

/**
 * @brief Called when a process refcount goes to zero
 *
 * Do not call this function directly, use PROC_RELEASE(), which will
 * call this function when the refcount hits zero.
 *
 * @param proc The process
 */
void proc_inactive(struct proc* proc);

/**
 * @brief Attach a thread to a process
 *
 * @param proc The process to attach the thread to
 * @param thread The thread to attach
 */
void proc_thread_attach(struct proc* proc, struct thread* thread);

/**
 * @brief Detach a thread from a process
 * @param thread The thread to detach
 */
void proc_thread_detach(struct thread* thread);

#define PROC_HOLD(p) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(p), struct proc*), "__builtin_types_compatible_p(typeof(p), struct proc*)"); \
		atomic_add_fetch(&(p)->refcnt, 1); \
	} while (0)
#define PROC_RELEASE(p) \
	do { \
		static_assert(__builtin_types_compatible_p(typeof(p), struct proc*), "__builtin_types_compatible_p(typeof(p), struct proc*)"); \
		if (atomic_sub_fetch(&(p)->refcnt, 1) == 0) \
			proc_inactive(p); \
	} while (0)
