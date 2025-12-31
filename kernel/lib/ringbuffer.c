#include <lunar/asm/wrap.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/lib/string.h>
#include <lunar/mm/heap.h>

#define RB_KMALLOC_LIMIT PAGE_SIZE

int ringbuffer_init(struct ringbuffer* rb, size_t capacity) {
	if (capacity & (capacity - 1))
		return -EINVAL;
	rb->buffer = capacity < RB_KMALLOC_LIMIT ? kzalloc(capacity, MM_ZONE_NORMAL) : vmalloc(capacity);
	if (!rb->buffer)
		return -ENOMEM;

	rb->capacity = capacity;
	rb->read = 0;
	rb->write = 0;

	return 0;
}

void ringbuffer_destroy(struct ringbuffer* rb) {
	if (rb->capacity < RB_KMALLOC_LIMIT)
		kfree(rb->buffer);
	else
		vfree(rb->buffer);
}

size_t ringbuffer_write(struct ringbuffer* rb, const void* src, size_t count) {
	size_t space = ringbuffer_space(rb);
	if (count > space)
		count = space;

	u8* to = rb->buffer;
	const u8* from = src;
	for (size_t i = 0; i < count; i++) {
		to[rb->write] = from[i];
		rb->write = (rb->write + 1) & (rb->capacity - 1);
	}

	return count;
}

size_t ringbuffer_read(struct ringbuffer* rb, void* dest, size_t count) {
	size_t space = ringbuffer_size(rb);
	if (count > space)
		count = space;

	const u8* from = rb->buffer;
	u8* to = dest;
	for (size_t i = 0; i < count; i++) {
		if (to)
			to[i] = from[rb->read];
		rb->read = (rb->read + 1) & (rb->capacity - 1);
	}

	return count;
}

ssize_t ringbuffer_peek(struct ringbuffer* rb, size_t* cursor, void* dest, size_t count) {
	size_t r = *cursor;
	if (r >= rb->capacity)
		return -EINVAL;

	size_t avail = rb->write >= r ? rb->write - r : rb->capacity - (r - rb->write);
	if (count > avail)
		count = avail;

	const u8* from = rb->buffer;
	u8* to = dest;
	for (size_t i = 0; i < count; i++) {
		to[i] = from[r];
		r = (r + 1) & (rb->capacity - 1);
	}

	*cursor = r;
	return count;
}
