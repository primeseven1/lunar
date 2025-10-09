#pragma once

enum system_statuses {
	INIT_STATUS_NOTHING,
	INIT_STATUS_MM,
	INIT_STATUS_SCHED,
	INIT_STATUS_FINISHED
};

/**
 * @brief Get the current status of initialization
 *
 * This function is primarily used to properly initialize optional drivers.
 *
 * @return The current initialization status
 */
int init_status_get(void);
