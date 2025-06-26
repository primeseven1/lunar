#include <crescent/types.h>
#include <crescent/core/cpu.h>
#include <crescent/core/io.h>
#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/core/panic.h>
#include "sched.h"

#define I8253_FREQ 1193182
#define I8253_CHANNEL0 0x40
#define I8253_COMMAND 0x43
#define I8253_MAX_MS 54

#define I8253_WAIT_LENGTH 10u

static void __i8253_wait(u8 ms) {
	u16 count = (I8253_FREQ / 1000) * ms;

	outb(I8253_COMMAND, 0x30);
	outb(I8253_CHANNEL0, count & 0xFF);
	outb(I8253_CHANNEL0, count >> 8);

	u16 current;
	do {
		u8 low = inb(I8253_CHANNEL0);
		u8 high = inb(I8253_CHANNEL0);
		current = (high << 8) | low;
	} while (current);
}


static void i8253_wait(unsigned long ms) {
	while (ms > I8253_MAX_MS) {
		__i8253_wait(I8253_MAX_MS);
		ms -= I8253_MAX_MS;
	}

	if (ms)
		__i8253_wait(ms);
}

static void timer(const struct isr* isr, struct context* ctx) {
	(void)isr;
	(void)ctx;
	sched_switch(ctx);
}

static spinlock_t i8253_lock = SPINLOCK_INITIALIZER;

static struct irq timer_irq = {
	.irq = -1,
	.eoi = apic_eoi
};

void sched_timer_init(void) {
	spinlock_lock(&i8253_lock);
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, U32_MAX);
	i8253_wait(I8253_WAIT_LENGTH);
	lapic_write(LAPIC_REG_LVT_TIMER, 1 << 16);
	spinlock_unlock(&i8253_lock);

	u32 ticks = U32_MAX - lapic_read(LAPIC_REG_TIMER_CURRENT);

	const struct isr* lapic_timer_isr = interrupt_register(&timer_irq, timer);
	if (!lapic_timer_isr)
		panic("Failed to allocate LAPIC timer ISR");

	lapic_write(LAPIC_REG_LVT_TIMER, lapic_timer_isr->vector | (1 << 17));
	lapic_write(LAPIC_REG_TIMER_DIVIDE, 0x03);
	lapic_write(LAPIC_REG_TIMER_INITIAL, ticks);

	printk(PRINTK_DBG "sched: LAPIC timer calibrated at %u ticks per %u ms on CPU %u\n", 
			ticks, I8253_WAIT_LENGTH, current_cpu()->processor_id);
}
