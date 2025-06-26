#pragma once

enum system_statuses {
	INIT_STATUS_NOTHING,
	INIT_STATUS_MM,
	INIT_STATUS_SCHED
};

int init_status_get(void);
