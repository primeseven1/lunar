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

#define WAIT_LENGTH 5000u

static u32 lapic_timer_get_ticks_for_preempt(void) {
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03); /* Set the divisor to 16 */
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX); /* Set the initial count to the maximum */
	timekeeper_stall(WAIT_LENGTH);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16); /* Stop timer */

	/* Now just return the difference of the initial count and the current count */
	return U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT); 
}

static void do_preempt(struct context* context) {
	struct cpu* cpu = current_cpu();
	spinlock_lock(&cpu->thread_lock);

	struct thread* current = cpu->current_thread;
	if (atomic_load(&current->state, ATOMIC_SEQ_CST) == THREAD_STATE_RUNNING)
		atomic_store(&current->state, THREAD_STATE_RUNNABLE, ATOMIC_SEQ_CST);

	struct thread* new_thread = select_new_thread(current);
	if (!new_thread) {
		new_thread = select_new_thread(cpu->thread_queue);
		if (unlikely(!new_thread))
			panic("no runnable threads! (idle thread died?)");
	}

	if (new_thread == current)
		goto out;

	/* This will allow the interrupt handler to restore the general purpose registers for us */
	current->ctx = *context;
	*context = new_thread->ctx;
	if (current->proc != new_thread->proc)
		vmm_switch_mm_struct(new_thread->proc->mm_struct);

	cpu->current_thread = new_thread;
out:
	atomic_store(&new_thread->state, THREAD_STATE_RUNNING, ATOMIC_SEQ_CST);
	spinlock_unlock(&cpu->thread_lock);
}

static void lapic_timer(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	do_preempt(ctx);
}

static struct irq timer_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

static const struct isr* lapic_timer_isr = NULL;

void sched_preempt_init(void) {
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
