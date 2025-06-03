#pragma once

enum system_statuses {
	INIT_STATUS_NOTHING,
	INIT_STATUS_BASIC /* Things up to kmalloc have been initialized */
};

int init_status_get(void);
