#pragma once

#include <crescent/core/interrupt.h>
#include <crescent/mm/vmm.h>

typedef int pid_t;

struct task {
	pid_t pid;
	struct {
		struct context ctx;
		__attribute__((aligned(16))) u8 fxsave[512]; /* Unused at the moment */
	};
	struct vmm_ctx vmm_ctx;
	struct task* parent, *children, *next;
};

void sched_init(void);
