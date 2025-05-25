#pragma once

#include <crescent/types.h>
#include <crescent/asm/errno.h>

#define PRINTK_DBG_N '\006'
#define PRINTK_INFO_N '\005'
#define PRINTK_WARN_N '\004'
#define PRINTK_ERR_N '\003'
#define PRINTK_CRIT_N '\002'
#define PRINTK_EMERG_N '\001'

#define PRINTK_DBG "\001\006"
#define PRINTK_INFO "\001\005"
#define PRINTK_WARN "\001\004"
#define PRINTK_ERR "\001\003"
#define PRINTK_CRIT "\001\002"
#define PRINTK_EMERG "\001\001"

struct printk_msg {
	const char* msg;
	unsigned int msg_level, global_level;
	size_t len;
};

int printk_set_hook(void (*hook)(const struct printk_msg*));
int printk_remove_hook(void (*hook)(const struct printk_msg*));
int printk_set_level(unsigned int level);

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
