#include <lunar/asm/wrap.h>
#include <lunar/core/panic.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/apic.h>
#include <lunar/core/printk.h>
#include <lunar/core/trace.h>
#include <lunar/init/status.h>
#include <lunar/lib/format.h>

static SPINLOCK_DEFINE(panic_lock);

_Noreturn void panic(const char* fmt, ...) {
	static char panic_msg[257];

	local_irq_disable();
	if (!spinlock_try_lock(&panic_lock))
		goto end;

	/* 
	 * Stop every CPU by sending an NMI, the NMI handler just panics, 
	 * so it will end when the CPU's can't grab the lock.
	 */
	if (init_status_get() >= INIT_STATUS_SCHED)
		apic_send_ipi(NULL, NULL, APIC_IPI_CPU_OTHERS, false);

	va_list va;
	va_start(va, fmt);

	/* Since we sent an NMI, there is no gauruntee the lock isn't held, so release it */
	printk_in_panic();

	vsnprintf(panic_msg, sizeof(panic_msg), fmt, va);
	dump_stack();
	printk(PRINTK_EMERG "Kernel panic - %s\n", panic_msg);

	va_end(va);
end:
	while (1)
		cpu_halt();
}
