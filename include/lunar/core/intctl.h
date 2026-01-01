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
	int (*install)(int, const struct isr*, const struct cpu*); /* Install an IRQ handler. IRQ's are OFF */
	int (*uninstall)(int); /* Remove an IRQ handler, IRQ's are OFF */
	int (*send_ipi)(const struct cpu*, const struct isr*, int); /* Send an IPI to another CPU. IRQ's are OFF */
	int (*enable)(int); /* Unmask an IRQ. IRQ's are OFF */
	int (*disable)(int); /* Mask an IRQ. IRQ's are OFF */
	int (*eoi)(int); /* Interrupt controller EOI signal */
	int (*wait_pending)(int); /* Wait for any pending interrupts in the controller's IRR. IRQ's are ON. Called on the CPU that handles the IRQ */
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
 * The interrupt will be masked after returning
 *
 * @param irq The IRQ to install
 * @param isr The ISR that handles the IRQ
 * @param cpu The target CPU to run the IRQ on
 *
 * @return Returns -errno as a pointer on failure
 */
struct irq* intctl_install_irq(int irq, const struct isr* isr, struct cpu* cpu);

/**
 * @brief Uninstall an IRQ handler
 * @param irq The IRQ to remove
 * @return -errno on failure
 */
int intctl_uninstall_irq(struct irq* irq);

/**
 * @brief Send an IPI to another CPU
 *
 * @param cpu The CPU to send the IPI to
 * @param isr The ISR for that CPU to run
 * @param flags Flags for how to send the IPI
 *
 * @return -errno on failure
 */
int intctl_send_ipi(const struct cpu* cpu, const struct isr* isr, int flags);

void intctl_enable_irq(const struct irq* irq);

void intctl_disable_irq(const struct irq* irq);

void intctl_wait_pending(const struct irq* irq);

const struct intctl_timer* intctl_get_timer(void);

void intctl_init_bsp(void);
void intctl_init_ap(void);
