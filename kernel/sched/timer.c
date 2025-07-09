#include <crescent/types.h>
#include <crescent/common.h>
#include <crescent/core/cpu.h>
#include <crescent/core/io.h>
#include <crescent/core/timekeeper.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include "sched.h"

static void timer(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	sched_switch(ctx);
}

static struct irq timer_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

#define USEC_CALIBRATION_TIME 10000ul

void sched_timer_init(void) {
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX);
	timekeeper_stall(USEC_CALIBRATION_TIME);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16);

	u32 ticks = U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT);

	const struct isr* lapic_timer_isr = interrupt_register(&timer_irq, timer);
	assert(lapic_timer_isr != NULL);

	lapic_write(LAPIC_REG_LVT_TIMER, lapic_timer_isr->vector | (1 << 17));
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);

	printk(PRINTK_DBG "sched: LAPIC timer calibrated at %u ticks per %lu us on CPU %u\n", 
			ticks, USEC_CALIBRATION_TIME, current_cpu()->processor_id);
}
