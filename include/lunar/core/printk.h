#pragma once

#include <lunar/types.h>
#include <lunar/asm/errno.h>

#define PRINTK_DBG_N '\006'
#define PRINTK_INFO_N '\005'
#define PRINTK_WARN_N '\004'
#define PRINTK_ERR_N '\003'
#define PRINTK_CRIT_N '\002'
#define PRINTK_EMERG_N '\001'
#define PRINTK_MAX_N PRINTK_DBG_N

#define PRINTK_DBG "\001\006"
#define PRINTK_INFO "\001\005"
#define PRINTK_WARN "\001\004"
#define PRINTK_ERR "\001\003"
#define PRINTK_CRIT "\001\002"
#define PRINTK_EMERG "\001\001"

struct printk_msg {
	int level, global_level;
	char time[25];
	u8 level_count;
	char msg[1024];
};

/**
 * @brief Set a printk hook
 *
 * The function will be called regardless of the printk level set.
 * This is useful for when doing something like outputting messages to the host terminal
 * when ran in a virtual machine.
 *
 * There is a set number of hooks that can be used, and an error is returned once that has
 * been reached.
 *
 * @param hook The hook to use
 * @return -errno on failure
 */
int printk_set_hook(void (*hook)(const struct printk_msg*));

/**
 * @brief Remove a printk hook
 * @param hook The hook to remove
 * @return -errno on failure
 */
int printk_remove_hook(void (*hook)(const struct printk_msg*));

/**
 * @brief Set the printk level
 * @param level The new level
 * @return -EINVAL if the level is invalid, 0 on success 
 */
int printk_set_level(int level);

/**
 * @brief Print a formatted string to the kernel log
 *
 * The maximum length of a message is 1024 characters.
 *
 * @param fmt The format string
 * @param va Variable argument list
 *
 * @return The length of the message
 */
int vprintk(const char* fmt, va_list va);

/**
 * @brief Print a formatted string to the kernel log
 *
 * The maximum length of a message is 1024 characters.
 *
 * @param fmt The format string
 * @param ... Variable arguments
 *
 * @return The length of the message
 */
__attribute__((format(printf, 1, 2)))
int printk(const char* fmt, ...);

void printk_init(void);

void printk_sched_gone(void);
