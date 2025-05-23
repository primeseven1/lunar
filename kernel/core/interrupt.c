#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>

__asmlinkage void __isr_entry(struct ctx* ctx);
__asmlinkage void __isr_entry(struct ctx* ctx) {
	panic("ISR: %lu", ctx->int_num);
}

extern void (*const isr_table[256]);

struct idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 reserved;
} __attribute__((packed));

static struct idt_entry idt_entries[256];

__asmlinkage void asm_idt_load(struct idt_entry* entries, size_t size);

void interrupts_init(void) {
	memset(idt_entries, 0, sizeof(idt_entries));
        for (size_t i = 0; i < ARRAY_SIZE(isr_table); i++) {
		idt_entries[i].handler_low = (uintptr_t)isr_table[i] & 0xFFFF;
		idt_entries[i].cs = 0x08;
		idt_entries[i].ist = i == 18 ? 1 : i == 8 ? 2 : i == 2 ? 3 : 0;
		idt_entries[i].flags = 0x8e;
		idt_entries[i].handler_mid = ((uintptr_t)isr_table[i] >> 16) & 0xFFFF;
		idt_entries[i].handler_high = (uintptr_t)isr_table[i] >> 32;
		idt_entries[i].reserved = 0;
        }

	asm_idt_load(idt_entries, sizeof(idt_entries));
}
