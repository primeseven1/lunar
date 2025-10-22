#include <lunar/core/module.h>
#include "internal.h"

static int tmpfs_init(void) {
	return vfs_register(&tmpfs_type);
}

MODULE("fs-tmpfs", INIT_STATUS_SCHED, tmpfs_init);
