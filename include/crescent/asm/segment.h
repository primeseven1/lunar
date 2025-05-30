#pragma once

#define SEGMENT_KERNEL_CODE 0x08
#define SEGMENT_KERNEL_DATA 0x10
#define SEGMENT_USER_CODE 0x18
#define SEGMENT_USER_CODE_32 0x20
#define SEGMENT_USER_DATA 0x28
#define SEGMENT_TASK_STATE 0x30

#ifndef __ASSEMBLER__

/**
 * @brief Initialize the global descriptor table
 */
void segments_init(void);

#endif /* __ASSEMBLER__ */
