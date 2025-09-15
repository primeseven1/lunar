#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/asm/segment.h>
#include <crescent/core/cpu.h>
#include <crescent/core/io.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include <crescent/core/timekeeper.h>
#include <crescent/mm/mm.h>
#include <crescent/mm/vmm.h>
#include "internal.h"

#define TIMER_TRIGGER_TIME_USEC 1000u

static u32 lapic_timer_get_ticks_for_preempt(void) {
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03); /* Set the divisor to 16 */
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX); /* Set the initial count to the maximum */
	timekeeper_stall(TIMER_TRIGGER_TIME_USEC);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16); /* Stop timer */

	/* Now just return the difference of the initial count and the current count */
	return U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT); 
}

static void lapic_timer(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	sched_tick();
}

static struct irq timer_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

static struct isr* lapic_timer_isr = NULL;

void preempt_cpu_init(void) {
	if (!lapic_timer_isr) {
		lapic_timer_isr = interrupt_alloc();
		if (unlikely(!lapic_timer_isr))
			panic("Failed to allocate LAPIC timer ISR");
		interrupt_register(lapic_timer_isr, &timer_irq, lapic_timer);
	}

	u32 ticks = lapic_timer_get_ticks_for_preempt();
	lapic_write(LAPIC_REG_LVT_TIMER, interrupt_get_vector(lapic_timer_isr) | (1 << 17));
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);

	printk(PRINTK_DBG "sched: LAPIC timer calibrated at %u ticks per %u us on CPU %u\n", 
			ticks, TIMER_TRIGGER_TIME_USEC, current_cpu()->processor_id);
}
