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
#include "sched.h"

#define WAIT_LENGTH 10000u

static u32 lapic_timer_get_ticks_for_preempt(void) {
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03); /* Set the divisor to 16 */
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX); /* Set the initial count to the maximum */
	timekeeper_stall(WAIT_LENGTH);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16); /* Stop timer */

	/* Now just return the difference of the initial count and the current count */
	return U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT); 
}

/* Needs to be as simple as possible, don't want to stay too long in this interrupt */
static void do_preempt(struct context* context) {
	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->current_thread;

	struct thread* next = get_next_thread();
	if (!next)
		panic("no runnable threads!");
	if (next == current)
		return;

	/* 
	 * Will add extended states later, we can just let the interrupt 
	 * handler restore the general purpose registers for us.
	 */
	current->ctx.general = *context;
	*context = next->ctx.general;

	if (current->proc != next->proc)
		vmm_switch_mm_struct(next->proc->mm_struct);

	if (thread_state_get(current) == THREAD_STATE_RUNNING)
		thread_state_set(current, THREAD_STATE_READY);
	thread_state_set(next, THREAD_STATE_RUNNING);

	cpu->current_thread = next;
}

static void lapic_timer(const struct isr* isr, struct context* ctx) {
	(void)isr;
	do_preempt(ctx);
}

static struct irq timer_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

static const struct isr* lapic_timer_isr = NULL;

void preempt_init(void) {
	if (!lapic_timer_isr) {
		lapic_timer_isr = interrupt_register(&timer_irq, lapic_timer);
		assert(lapic_timer_isr != NULL);
	}

	u32 ticks = lapic_timer_get_ticks_for_preempt();
	lapic_write(LAPIC_REG_LVT_TIMER, lapic_timer_isr->vector | (1 << 17));
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);

	printk(PRINTK_DBG "sched: LAPIC timer calibrated at %u ticks per %u us on CPU %u\n", 
			ticks, WAIT_LENGTH, current_cpu()->processor_id);
}
