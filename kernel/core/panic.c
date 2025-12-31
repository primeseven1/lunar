#include <lunar/asm/wrap.h>
#include <lunar/core/panic.h>
#include <lunar/core/interrupt.h>
#include <lunar/core/cpu.h>
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

	smp_send_stop();
	printk_sched_gone();

	va_list va;
	va_start(va, fmt);

	vsnprintf(panic_msg, sizeof(panic_msg), fmt, va);
	dump_stack();
	printk(PRINTK_EMERG "Kernel panic - %s\n", panic_msg);

	va_end(va);
end:
	while (1)
		cpu_halt();
}
