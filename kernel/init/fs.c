#include <lunar/core/vfs.h>
#include <lunar/core/module.h>
#include <lunar/core/printk.h>
#include <lunar/lib/string.h>
#include <lunar/lib/format.h>
#include "internal.h"

extern struct filesystem_type _ld_kernel_fstypes_start[];
extern struct filesystem_type _ld_kernel_fstypes_end[];

void fs_drivers_load(void) {
	struct filesystem_type* start = _ld_kernel_fstypes_start;
	struct filesystem_type* end = _ld_kernel_fstypes_end;

	const char* prefix = "fs-";

	size_t count = ((uintptr_t)end - (uintptr_t)start) / sizeof(*start);
	for (size_t i = 0; i < count; i++) {
		size_t size = strlen(start[i].name) + __builtin_strlen(prefix) + 1;
		char* modname = kmalloc(size, MM_ZONE_NORMAL);
		if (unlikely(!modname))
			break;
		snprintf(modname, size, "%s%s", prefix, start[i].name);
		int err = module_load(modname);
		if (err)
			printk(PRINTK_WARN "init: Failed to load fs %s: %i\n", modname, err);
		kfree(modname);
	}
}
