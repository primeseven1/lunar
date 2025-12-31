#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/abi.h>

struct intctl_timer_ops {
	int (*setup)(const struct isr*, time_t usec);
	void (*on_interrupt)(void);
};

struct intctl_timer {
	const char* name;
	const struct intctl_timer_ops* ops;
};

struct intctl_ops {
	int (*init_bsp)(void);
	int (*init_ap)(void);
	int (*install)(int, const struct isr*, const struct cpu*);
	int (*uninstall)(int);
	int (*send_ipi)(const struct cpu*, const struct isr*, int);
	int (*enable)(int);
	int (*disable)(int);
	int (*eoi)(int);
	int (*wait_pending)(int);
};

struct intctl {
	const char* name;
	int rating;
	const struct intctl_ops* ops;
	const struct intctl_timer* timer;
};

enum intctl_ipi_flags {
	INTCTL_IPI_CRITICAL = (1 << 0)
};

#define __intctl __attribute__((section(".intctl"), aligned(8), used))

/**
 * @brief Send an end of interrupt signal to the interrupt controller
 *
 * IRQ is allowed to be NULL. When this happens, it is assumed that the interrupt
 * was an IPI and -1 is given to the interrupt controller driver.
 *
 * @param irq The IRQ that resulted in the interrupt
 */
void intctl_eoi(const struct irq* irq);

/**
 * @brief Install an IRQ handler
 *
 * If irq is NULL, the function sets isr->need_eoi to true, and intctl->install is called
 * with an IRQ number of -1. The ISR is then considered to be used for an IPI.
 *
 * The interrupt will be masked after returning
 *
 * @param irq The IRQ to install
 * @param isr The ISR that handles the IRQ
 * @param cpu The target CPU to run the IRQ on
 */
struct irq* intctl_install_irq(int irq, const struct isr* isr, struct cpu* cpu);

int intctl_uninstall_irq(struct irq* irq);

int intctl_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags);

void intctl_enable_irq(const struct irq* irq);

void intctl_disable_irq(const struct irq* irq);

void intctl_wait_pending(const struct irq* irq);

const struct intctl_timer* intctl_get_timer(void);

void intctl_init_bsp(void);
void intctl_init_ap(void);
