#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/spinlock.h>
#include <lunar/trace.h>
#include <lunar/format.h>
#include <lunar/smp.h>
#include <lunar/irq.h>
#include <arch/processor.h>

__attribute__((format(printf, 1, 2)))
_Noreturn void panic(const char* fmt, ...) {
	static char panic_msg[257];
	static SPINLOCK_DEFINE(panic_lock);

	local_irq_disable();
	if (!spinlock_try_acquire(&panic_lock))
		goto end;

	smp_send_stop();
	printk_disable_ringbuffer_and_flush();

	va_list va;
	va_start(va, fmt);

	dump_stack();
	vsnprintf(panic_msg, sizeof(panic_msg), fmt, va);
	printk(PRINTK_EMERG "Kernel panic - %s\n", panic_msg);

	va_end(va);
end:
	while (1)
		arch_cpu_idle();
}
