#include <crescent/asm/wrap.h>
#include <crescent/core/panic.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/printk.h>
#include <crescent/core/trace.h>
#include <crescent/lib/format.h>

_Noreturn void panic(const char* fmt, ...) {
	static char panic_msg[257];

	local_irq_disable();

	va_list va;
	va_start(va, fmt);

	printk_emerg_release_lock();
	vsnprintf(panic_msg, sizeof(panic_msg), fmt, va);
	dump_stack();
	printk(PRINTK_EMERG "Kernel panic - %s\n", panic_msg);

	va_end(va);
	while (1)
		cpu_halt();
}
