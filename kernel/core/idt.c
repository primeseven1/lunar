#include <crescent/common.h>
#include <crescent/compiler.h>
#include <crescent/asm/segment.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/panic.h>
#include <crescent/lib/string.h>
#include <crescent/mm/vmm.h>
#include "idt.h"

struct idt_entry {
	u16 handler_low;
	u16 cs;
	u8 ist;
	u8 flags;
	u16 handler_mid;
	u32 handler_high;
	u32 __reserved;
} __attribute__((packed));

static struct idt_entry* idt = NULL;
extern const uintptr_t isr_table[INTERRUPT_COUNT];

static void __idt_init(void) {
	for (size_t i = 0; i < INTERRUPT_COUNT; i++) {
		idt[i].handler_low = isr_table[i] & 0xFFFF;
		idt[i].cs = SEGMENT_KERNEL_CODE;
		idt[i].ist = i == 18 ? 1 : i == 8 ? 2 : i == 2 ? 3 : 0;
		idt[i].flags = 0x8e;
		idt[i].handler_mid = (isr_table[i] >> 16) & 0xFFFF;
		idt[i].handler_high = isr_table[i] >> 32;
		idt[i].__reserved = 0;
	}
}

void idt_init(void) {
	const size_t idt_size = sizeof(*idt) * INTERRUPT_COUNT;
	if (!idt) {
		idt = kmap(MM_ZONE_NORMAL, idt_size, MMU_READ | MMU_WRITE);
		if (!idt)
			panic("Failed to initialize IDT");
		__idt_init();
		kprotect(idt, idt_size, MMU_READ);
	}

	struct {
		u16 limit;
		struct idt_entry* pointer;
	} __attribute__((packed)) idtr = {
		.limit = idt_size - 1,
		.pointer = idt
	};
	__asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}
