#include <lunar/types.h>
#include <lunar/common.h>
#include <lunar/asm/segment.h>
#include <lunar/core/cpu.h>
#include <lunar/core/io.h>
#include <lunar/core/apic.h>
#include <lunar/core/printk.h>
#include <lunar/core/panic.h>
#include <lunar/core/softirq.h>
#include <lunar/core/timekeeper.h>
#include <lunar/mm/mm.h>
#include <lunar/mm/vmm.h>
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
	raise_softirq(SOFTIRQ_TIMER);
}

static struct isr* lapic_timer_isr = NULL;

void preempt_cpu_init(void) {
	if (!lapic_timer_isr) {
		lapic_timer_isr = interrupt_alloc();
		if (unlikely(!lapic_timer_isr))
			panic("Failed to allocate LAPIC timer ISR");
		interrupt_register(lapic_timer_isr, lapic_timer, apic_set_irq, -1, NULL, false);
	}

	u32 ticks = lapic_timer_get_ticks_for_preempt();
	lapic_write(LAPIC_REG_LVT_TIMER, interrupt_get_vector(lapic_timer_isr) | (1 << 17));
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);

	printk(PRINTK_DBG "sched: LAPIC timer calibrated at %u ticks per %u us on CPU %u\n", 
			ticks, TIMER_TRIGGER_TIME_USEC, current_cpu()->sched_processor_id);
	bug(register_softirq(sched_tick, SOFTIRQ_TIMER) != 0);
}
