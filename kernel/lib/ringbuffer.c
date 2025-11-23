#include <lunar/asm/wrap.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/lib/string.h>
#include <lunar/mm/heap.h>

static bool check_flags(int flags) {
	int count = 0;
	if (flags & RINGBUFFER_OVERWRITE)
		count++;
	if (flags & RINGBUFFER_BOUNDED)
		count++;
	if (flags & RINGBUFFER_DROP_NEW)
		count++;
	return count == 1;
}

int ringbuffer_init(struct ringbuffer* rb, int flags, size_t capacity, size_t element_size) {
	if (!check_flags(flags) || capacity < 16 || (capacity & (capacity - 1)))
		return -EINVAL;

	rb->buffer = kmalloc(capacity * element_size, MM_ZONE_NORMAL);
	if (!rb->buffer)
		return -ENOMEM;

	rb->flags = flags;
	rb->capacity = capacity;
	rb->element_size = element_size;
	rb->head = 0;
	rb->tail = 0;
	spinlock_init(&rb->lock);

	return 0;
}

int ringbuffer_enqueue(struct ringbuffer* rb, const void* element) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&rb->lock, &irq_flags);

	int err = 0;
	size_t next = (rb->head + 1) & (rb->capacity - 1);
	if (next == rb->tail) {
		if (rb->flags & RINGBUFFER_OVERWRITE)
			rb->tail = (rb->tail + 1) & (rb->capacity - 1);
		else if (rb->flags & RINGBUFFER_BOUNDED)
			err = -ENOSPC;
		else if (rb->flags & RINGBUFFER_DROP_NEW)
			goto out;
		if (err)
			goto out;
	}

	memcpy((u8*)rb->buffer + rb->head * rb->element_size, element, rb->element_size);
	rb->head = next;
out:
	spinlock_unlock_irq_restore(&rb->lock, &irq_flags);
	return err;
}

int ringbuffer_dequeue(struct ringbuffer* rb, void* element) {
	irqflags_t irq_flags;
	spinlock_lock_irq_save(&rb->lock, &irq_flags);

	int err = 0;
	if (rb->head == rb->tail) {
		err = -ENODATA;
		goto out;
	}

	memcpy(element, (u8*)rb->buffer + rb->tail * rb->element_size, rb->element_size);
	rb->tail = (rb->tail + 1) & (rb->capacity - 1);
out:
	spinlock_unlock_irq_restore(&rb->lock, &irq_flags);
	return err;
}

void ringbuffer_destroy(struct ringbuffer* rb) {
	kfree(rb->buffer);
	rb->buffer = NULL;
}
