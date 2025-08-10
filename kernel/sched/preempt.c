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

static u32 lapic_timer_get_ticks_for_preempt(void) {
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03); /* Set the divisor to 16 */
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX); /* Set the initial count to the maximum */
	timekeeper_stall(TIMER_TRIGGER_TIME_USEC);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16); /* Stop timer */

	/* Now just return the difference of the initial count and the current count */
	return U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT); 
}

/* Needs to be as simple as possible, don't want to stay too long in this interrupt */
static void do_preempt(void) {
	struct cpu* cpu = current_cpu();
	struct thread* current = cpu->runqueue.current;
	if (current == cpu->runqueue.idle)
		return;
	if (current->preempt_count > 0)
		return;
	if (current->time_slice && --current->time_slice == 0)
		cpu->need_resched = true;
}

static void lapic_timer(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;

	struct cpu* cpu = current_cpu();
	time_t now = timekeeper_get_nsec();

	spinlock_lock(&cpu->runqueue.lock);

	do_preempt();

	/* Wake up threads that are sleeping */
	struct thread* pos, *tmp;
	list_for_each_entry_safe(pos, tmp, &cpu->runqueue.sleeping, sleep_link) {
		if (now >= pos->wakeup_time) {
			atomic_store(&pos->state, THREAD_READY, ATOMIC_RELEASE);
			list_remove(&pos->sleep_link);
			rr_enqueue_thread(pos);
			if (cpu->runqueue.current == cpu->runqueue.idle)
				cpu->need_resched = true;
		}
	}

	spinlock_unlock(&cpu->runqueue.lock);
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
			ticks, TIMER_TRIGGER_TIME_USEC, current_cpu()->processor_id);
}
