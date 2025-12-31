#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/spinlock.h>
#include <lunar/mm/heap.h>

struct ringbuffer {
	void* buffer;
	size_t write, read;
	size_t capacity;
};

static inline size_t ringbuffer_size(struct ringbuffer* rb) {
	if (rb->write >= rb->read)
		return rb->write - rb->read;
	return rb->capacity - (rb->read - rb->write);
}

static inline size_t ringbuffer_space(struct ringbuffer* rb) {
	return rb->capacity - ringbuffer_size(rb);
}

int ringbuffer_init(struct ringbuffer* rb, size_t capacity);
void ringbuffer_destroy(struct ringbuffer* rb);
size_t ringbuffer_write(struct ringbuffer* rb, const void* src, size_t count);
size_t ringbuffer_read(struct ringbuffer* rb, void* dest, size_t count);
ssize_t ringbuffer_peek(struct ringbuffer* rb, size_t* cursor, void* dest, size_t count);
