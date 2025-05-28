#include <crescent/common.h>
#include <crescent/core/io.h>
#include "e9.h"

static const char* get_lvl_string(unsigned int level) {
	switch (level) {
	case PRINTK_DBG_N:
		return "\033[32m[DBG]\033[0m ";
	case PRINTK_INFO_N:
		return "\033[37m[INFO]\033[0m ";
	case PRINTK_WARN_N:
		return "\033[33m[WARN]\033[0m ";
	case PRINTK_ERR_N:
		return "\033[31m[ERR]\033[0m ";
	case PRINTK_CRIT_N:
		return "\033[31m[CRIT]\033[0m ";
	case PRINTK_EMERG_N:
		return "\033[31m[EMERG]\033[0m ";
	}

	return NULL;
}

void e9hack_printk_hook(const struct printk_msg* msg) {
	if (msg->msg_level > msg->global_level)
		return;

	const char* lvl_str = get_lvl_string(msg->msg_level);
	if (!lvl_str)
		return;

	while (*lvl_str)
		outb(0xe9, *lvl_str++);
	const char* _msg = msg->msg;
	while (*_msg)
		outb(0xe9, *_msg++);
}
