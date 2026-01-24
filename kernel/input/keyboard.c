#include <lunar/core/term.h>
#include <lunar/input/keyboard.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include <lunar/mm/heap.h>
#include "internal.h"

#define KEYBOARD_BUFFER_SIZE (256 * sizeof(struct kb_packet))
static LIST_HEAD_DEFINE(keyboard_list);

struct keyboard* keyboard_create(void) {
	struct keyboard* ret = kmalloc(sizeof(*ret), MM_ZONE_NORMAL);
	if (!ret)
		return NULL;

	int err = ringbuffer_init(&ret->rb, KEYBOARD_BUFFER_SIZE);
	if (err)
		return NULL;
	spinlock_init(&ret->rb_lock);
	semaphore_init(&ret->semaphore, 0);
	list_node_init(&ret->link);
	list_add(&keyboard_list, &ret->link);

	return ret;
}

void keyboard_destroy(struct keyboard* keyboard) {
	list_remove(&keyboard->link);
	ringbuffer_destroy(&keyboard->rb);
	kfree(keyboard);
}

static const char ascii_table[112] = {
	0, '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r', 
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
	'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	'7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\r', 0, '/', 0, 0, '\r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char ascii_table_upper[112] = {
       0, '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
       '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r',
       0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
       'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\r', 0, '/', 0, 0, '\r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

struct modifier_info {
	int flag;
	bool toggle;
};
static const struct modifier_info modifier_table[] = {
	[KEYCODE_LEFTCTRL] = { .flag = KB_PACKET_FLAG_LEFTCTRL, .toggle = false },
	[KEYCODE_RIGHTCTRL] = { .flag = KB_PACKET_FLAG_RIGHTCTRL, .toggle = false },
	[KEYCODE_CAPSLOCK] = { .flag = KB_PACKET_FLAG_CAPSLOCK, .toggle = true },
	[KEYCODE_LEFTALT] = { .flag = KB_PACKET_FLAG_LEFTALT, .toggle = false },
	[KEYCODE_RIGHTALT] = { .flag = KB_PACKET_FLAG_RIGHTALT, .toggle = false },
	[KEYCODE_LEFTSHIFT] = { .flag = KB_PACKET_FLAG_LEFTSHIFT, .toggle = false },
	[KEYCODE_RIGHTSHIFT] = { .flag = KB_PACKET_FLAG_RIGHTSHIFT, .toggle = false },
};

static inline bool is_modifier(unsigned int keycode) {
	switch (keycode) {
	case KEYCODE_LEFTCTRL:
	case KEYCODE_RIGHTCTRL:
	case KEYCODE_CAPSLOCK:
	case KEYCODE_LEFTALT:
	case KEYCODE_RIGHTALT:
	case KEYCODE_LEFTSHIFT:
	case KEYCODE_RIGHTSHIFT:
		return true;
	}

	return false;
}

void keyboard_send_packet(struct keyboard* keyboard, struct kb_packet* packet) {
	if (is_modifier(packet->keycode)) {
		const struct modifier_info* modinfo = &modifier_table[packet->keycode];
		if (modinfo->toggle)
			keyboard->flags ^= modinfo->flag;
		else if (packet->flags & KB_PACKET_FLAG_RELEASED)
			keyboard->flags &= ~modinfo->flag;
		else
			keyboard->flags |= modinfo->flag;
	}

	packet->flags |= keyboard->flags;
	const char* table = ascii_table;
	bool shift = ((packet->flags & KB_PACKET_FLAG_LEFTSHIFT) || (packet->flags & KB_PACKET_FLAG_RIGHTSHIFT));
	if (shift ^ (packet->flags & KB_PACKET_FLAG_CAPSLOCK))
		table = ascii_table_upper;
	packet->ascii = table[packet->keycode];

	irqflags_t irq;
	spinlock_lock_irq_save(&keyboard->rb_lock, &irq);

	if (ringbuffer_write(&keyboard->rb, packet, sizeof(*packet)) != 0)
		semaphore_signal(&keyboard->semaphore);

	spinlock_unlock_irq_restore(&keyboard->rb_lock, &irq);
}

int keyboard_wait(struct keyboard* keyboard, struct kb_packet* out_packet) {
	int err = semaphore_wait(&keyboard->semaphore, SCHED_SLEEP_INTERRUPTIBLE);
	if (err)
		return err;

	irqflags_t irq;
	spinlock_lock_irq_save(&keyboard->rb_lock, &irq);
	size_t count = ringbuffer_read(&keyboard->rb, out_packet, sizeof(*out_packet));
	spinlock_unlock_irq_restore(&keyboard->rb_lock, &irq);
	bug(count != sizeof(*out_packet));

	return 0;
}

/* reader() and keyboard_reader_thread_init() will get removed, this code is more just to see if the kernel isn't deadlocked or anything */

static int reader(void* arg) {
	struct keyboard* kb = arg;
	bool sysrq = false;

	while (1) {
		struct kb_packet packet;
		int rc = keyboard_wait(kb, &packet);
		if (rc || is_modifier(packet.keycode) || packet.keycode == KEYCODE_RESERVED)
			continue;

		bool pressed = !(packet.flags & KB_PACKET_FLAG_RELEASED);
		int sysrq_modifiers = KB_PACKET_FLAG_LEFTCTRL | KB_PACKET_FLAG_RIGHTCTRL;
		if (packet.flags & sysrq_modifiers && packet.keycode == KEYCODE_SYSREQ) {
			sysrq = pressed;
		} else if (pressed) {
			if (sysrq)
				do_sysrq(packet.keycode);
			else
				term_write(&packet.ascii, 1);
		}
	}

	return 0;
}

void keyboard_reader_thread_init(void) {
	if (list_empty(&keyboard_list))
		return;
	struct thread* thread = kthread_create(0, reader, list_first_entry(&keyboard_list, struct keyboard, link), "kbreader");
	if (thread)
		kthread_run(thread, SCHED_PRIO_DEFAULT);
}
