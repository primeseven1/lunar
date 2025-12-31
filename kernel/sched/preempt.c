#include <lunar/types.h>
#include <lunar/common.h>
#include <lunar/asm/segment.h>
#include <lunar/core/cpu.h>
#include <lunar/core/io.h>
#include <lunar/core/printk.h>
#include <lunar/core/intctl.h>
#include <lunar/core/panic.h>
#include <lunar/core/softirq.h>
#include <lunar/core/timekeeper.h>
#include <lunar/mm/mm.h>
#include <lunar/mm/vmm.h>
#include "internal.h"

static const struct intctl_timer* timer = NULL;
static struct isr* timer_isr = NULL;

static void do_timer(struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	raise_softirq(SOFTIRQ_TIMER);
	if (timer->ops->on_interrupt)
		timer->ops->on_interrupt();
}

void preempt_cpu_init(void) {
	if (!timer_isr) {
		timer_isr = interrupt_alloc();
		if (unlikely(!timer_isr))
			panic("Failed to allocate LAPIC timer ISR");
		interrupt_register(timer_isr, NULL, do_timer);
	}
	if (!timer) {
		timer = intctl_get_timer();
		if (!timer)
			panic("No timer available for preempt");
		printk("sched: per-cpu timer %s chosen\n", timer->name);
		bug(register_softirq(sched_tick, SOFTIRQ_TIMER) != 0);
	}

	if (unlikely(timer->ops->setup(timer_isr, 1000) != 0))
		panic("Failed to set up per-cpu timer for CPU %u", current_cpu()->sched_processor_id);
}
