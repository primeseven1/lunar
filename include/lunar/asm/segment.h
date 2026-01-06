#pragma once

#define SEGMENT_KERNEL_CODE 0x08
#define SEGMENT_KERNEL_DATA 0x10
#define SEGMENT_USER_CODE 0x18
#define SEGMENT_USER_DATA 0x20
#define SEGMENT_TASK_STATE 0x28

#ifndef __ASSEMBLER__

#include <lunar/types.h>

struct tss_descriptor {
	u32 __reserved0;
	void* rsp[3];
	u64 __reserved1;
	void* ist[7];
	u32 __reserved2;
	u32 __reserved3;
	u16 __reserved4;
	u16 iopb;
} __attribute__((packed));

/**
 * @brief Initialize the global descriptor table
 */
void segments_init(void);

#endif /* __ASSEMBLER__ */
