#include <lunar/core/io.h>
#include <lunar/core/term.h>
#include <lunar/core/printk.h>
#include <lunar/core/cpu.h>
#include <lunar/core/intctl.h>
#include <lunar/input/keyboard.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/sched/scheduler.h>
#include "i8042.h"

static const u8 normal_codes[128] = {
	KEYCODE_RESERVED,
	KEYCODE_ESCAPE,
	KEYCODE_1, KEYCODE_2, KEYCODE_3, KEYCODE_4, KEYCODE_5, KEYCODE_6, KEYCODE_7, KEYCODE_8, KEYCODE_9, KEYCODE_0, KEYCODE_MINUS, KEYCODE_EQUAL, KEYCODE_BACKSPACE,
	KEYCODE_TAB, KEYCODE_Q, KEYCODE_W, KEYCODE_E, KEYCODE_R, KEYCODE_T, KEYCODE_Y, KEYCODE_U, KEYCODE_I, KEYCODE_O, KEYCODE_P, KEYCODE_LEFTBRACE, KEYCODE_RIGHTBRACE,
	KEYCODE_ENTER, KEYCODE_LEFTCTRL,
	KEYCODE_A, KEYCODE_S, KEYCODE_D, KEYCODE_F, KEYCODE_G, KEYCODE_H, KEYCODE_J, KEYCODE_K, KEYCODE_L, KEYCODE_SEMICOLON, KEYCODE_APOSTROPHE,
	KEYCODE_GRAVE,
	KEYCODE_LEFTSHIFT,
	KEYCODE_BACKSLASH,
	KEYCODE_Z, KEYCODE_X, KEYCODE_C, KEYCODE_V, KEYCODE_B, KEYCODE_N, KEYCODE_M, KEYCODE_COMMA, KEYCODE_DOT, KEYCODE_SLASH,
	KEYCODE_RIGHTSHIFT,
	KEYCODE_KEYPADASTERISK,
	KEYCODE_LEFTALT,
	KEYCODE_SPACE,
	KEYCODE_CAPSLOCK,
	KEYCODE_F1, KEYCODE_F2, KEYCODE_F3, KEYCODE_F4, KEYCODE_F5, KEYCODE_F6, KEYCODE_F7, KEYCODE_F8, KEYCODE_F9, KEYCODE_F10,
	KEYCODE_NUMLOCK,
	KEYCODE_SCROLLLOCK,
	KEYCODE_KEYPAD7, KEYCODE_KEYPAD8, KEYCODE_KEYPAD9,
	KEYCODE_KEYPADMINUS, KEYCODE_KEYPAD4, KEYCODE_KEYPAD5, KEYCODE_KEYPAD6, KEYCODE_KEYPADPLUS,
	KEYCODE_KEYPAD1, KEYCODE_KEYPAD2, KEYCODE_KEYPAD3, KEYCODE_KEYPAD0,
	KEYCODE_KEYPADDOT,
	KEYCODE_RESERVED, KEYCODE_RESERVED, KEYCODE_RESERVED,
	KEYCODE_F11, KEYCODE_F12
};

static const u8 ext_codes[] = {
	[0x1C] = KEYCODE_KEYPADENTER,
	[0x1D] = KEYCODE_RIGHTCTRL, 
	[0x35] = KEYCODE_KEYPADSLASH,
	[0x37] = KEYCODE_SYSREQ,
	[0x38] = KEYCODE_RIGHTALT,
	[0x47] = KEYCODE_HOME,
	[0x48] = KEYCODE_UP,
	[0x49] = KEYCODE_PAGEUP,
	[0x4B] = KEYCODE_LEFT,
	[0x4D] = KEYCODE_RIGHT,
	[0x4F] = KEYCODE_END,
	[0x50] = KEYCODE_DOWN,
	[0x51] = KEYCODE_PAGEDOWN,
	[0x52] = KEYCODE_INSERT,
	[0x53] = KEYCODE_DELETE
};

static void i8042_irq_kbd(struct isr* isr, struct context* ctx) {
	(void)ctx;

	struct i8042_keyboard* device = isr->private;
	u8 scancode = i8042_data_read();

	/* Just return here, since another IRQ will be generated with the second keycode */
	if (scancode == 0xe0) {
		device->in_extended = true;
		return;
	}

	struct kb_packet packet;

	packet.flags = 0;
	if (scancode & 0x80) {
		packet.flags |= KB_PACKET_FLAG_RELEASED;
		scancode &= ~0x80;
	}

	if (unlikely((device->in_extended && scancode >= ARRAY_SIZE(ext_codes)) ||
				(!device->in_extended && scancode >= ARRAY_SIZE(normal_codes))))
		return;

	const u8* codes = device->in_extended ? ext_codes : normal_codes;
	device->in_extended = false;
	packet.keycode = codes[scancode];
	if (packet.keycode != KEYCODE_RESERVED)
		keyboard_send_packet(device->kbd, &packet);
}

int i8042_setup_irq_kbd(struct i8042_keyboard* device) {
	struct isr* isr = interrupt_alloc();
	if (!isr)
		return -ENOENT;
	struct irq* irq_struct = intctl_install_irq(device->irq_number, isr, current_cpu());
	if (IS_PTR_ERR(irq_struct)) {
		interrupt_free(isr);
		return PTR_ERR(irq_struct);
	}

	isr->private = device;
	device->isr = isr;
	device->irq_struct = irq_struct;

	interrupt_register(isr, irq_struct, i8042_irq_kbd);
	irq_enable(irq_struct);
	return 0;
}
