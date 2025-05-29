#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>
#include <crescent/mm/vmm.h>

__asmlinkage void __isr_entry(struct ctx* ctx);
__asmlinkage void __isr_entry(struct ctx* ctx) {
	panic("ISR: %lu", ctx->int_num);
}

struct idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 reserved;
} __attribute__((packed));

#define IDT_ENTRY_COUNT 256
#define IDT_SIZE (sizeof(struct idt_entry) * IDT_ENTRY_COUNT)

extern void (*const isr_table[256]);
static struct idt_entry* idt_entries = NULL;

__asmlinkage void asm_idt_load(struct idt_entry* entries, size_t size);

static void __interrupts_init(void) {
	/* sidt doesn't leak the address of the kernel when done this way */
	idt_entries = kmap(MM_ZONE_NORMAL, IDT_SIZE, MMU_READ | MMU_WRITE);
	if (!idt_entries)
		panic("Failed to allocate IDT");

	/* Set all the entries in the ISR table */
        for (size_t i = 0; i < ARRAY_SIZE(isr_table); i++) {
		idt_entries[i].handler_low = (uintptr_t)isr_table[i] & 0xFFFF;
		idt_entries[i].cs = 0x08;
		idt_entries[i].ist = i == 18 ? 1 : i == 8 ? 2 : i == 2 ? 3 : 0;
		idt_entries[i].flags = 0x8e;
		idt_entries[i].handler_mid = ((uintptr_t)isr_table[i] >> 16) & 0xFFFF;
		idt_entries[i].handler_high = (uintptr_t)isr_table[i] >> 32;
		idt_entries[i].reserved = 0;
        }

	/* Now set the page(s) as read only */
	kprotect(idt_entries, IDT_SIZE, MMU_READ);
}

void interrupts_init(void) {
	if (!idt_entries)
		__interrupts_init();

	asm_idt_load(idt_entries, IDT_SIZE);
}
