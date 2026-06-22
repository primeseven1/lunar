#pragma once

#include <arch/context.h>

struct isr;

struct arch_isr {
	unsigned int id, flags;
	void (*ehandler)(struct isr*, struct arch_context*);
	bool need_eoi;
};

int arch_register_isr(struct isr* isr);
int arch_unregister_isr(struct isr* isr);
