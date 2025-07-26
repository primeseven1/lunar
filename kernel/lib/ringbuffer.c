#include <crescent/lib/ringbuffer.h>
#include <crescent/lib/string.h>
#include <crescent/mm/heap.h>

int ringbuffer_init(struct ringbuffer* rb, unsigned long capacity, size_t element_size) {
	if (capacity == 0 || (capacity & (capacity - 1)))
		return -EINVAL;

	rb->buffer = kmalloc(element_size * capacity, MM_ZONE_NORMAL);
	if (!rb->buffer)
		return -ENOMEM;

	rb->element_size = element_size;
	rb->capacity = capacity;
	rb->head = 0;
	rb->tail = 0;

	return 0;
}

int ringbuffer_enqueue(struct ringbuffer* rb, const void* element) {
	unsigned long head = rb->head;
	unsigned long tail = rb->tail;

	/* Check to see if the buffer is full */
	if (((head + 1) & (rb->capacity - 1)) == tail)
		return -ENOSPC;

	unsigned long mask = rb->capacity - 1;
	memcpy((u8*)rb->buffer + ((head & mask) * rb->element_size), element, rb->element_size);
	rb->head = (head + 1) & mask;

	return 0;
}

int ringbuffer_dequeue(struct ringbuffer* rb, void* element) {
	unsigned long head = rb->head;
	unsigned long tail = rb->tail;

	/* See if the ringbuffer is empty */
	if (head == tail)
		return -ENODATA;

	unsigned long mask = rb->capacity - 1;
	memcpy(element, (u8*)rb->buffer + ((tail & mask) * rb->element_size), rb->element_size);
	rb->tail = (tail + 1) & mask;

	return 0;
}
