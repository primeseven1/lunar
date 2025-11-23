#include <lunar/asm/wrap.h>
#include <lunar/lib/ringbuffer.h>
#include <lunar/lib/string.h>
#include <lunar/mm/heap.h>

static inline size_t rb_slot_size(const struct ringbuffer* rb) {
	size_t raw = sizeof(struct rb_slot) + rb->element_size;
	size_t align = BIGGEST_ALIGNMENT;
	return (raw + align - 1) & ~(align - 1);
}

static inline struct rb_slot* rb_slot_at(const struct ringbuffer* rb, size_t index) {
	size_t stride = rb_slot_size(rb);
	size_t pos = index & (rb->capacity - 1);
	return (struct rb_slot*)((u8*)rb->buffer + pos * stride);
}

int ringbuffer_init(struct ringbuffer* rb, unsigned int mode, size_t capacity, size_t element_size) {
	if (mode > __RINGBUFFER_MODE_MAX || capacity == 0 || (capacity & (capacity - 1)))
		return -EINVAL;

	rb->mode = mode;
	rb->capacity = capacity;
	rb->element_size = element_size;
	atomic_store(&rb->head, 0);
	atomic_store(&rb->tail, 0);

	size_t stride = rb_slot_size(rb);
	rb->buffer = kmalloc(capacity * stride, MM_ZONE_NORMAL);
	if (!rb->buffer)
		return -ENOMEM;

	for (size_t i = 0; i < capacity; i++) {
		struct rb_slot* buf = rb_slot_at(rb, i);
		atomic_store_explicit(&buf->seq, i, ATOMIC_RELAXED);
	}

	return 0;
}

int ringbuffer_enqueue(struct ringbuffer* rb, const void* element) {
	while (1) {
		size_t pos = atomic_load_explicit(&rb->head, ATOMIC_RELAXED);
		struct rb_slot* buf = rb_slot_at(rb, pos);
		size_t seq = atomic_load_explicit(&buf->seq, ATOMIC_ACQUIRE);

		ssize_t diff = (ssize_t)seq - (ssize_t)pos;
		if (diff == 0) {
			if (atomic_compare_exchange_weak_explicit(&rb->head, &pos, pos + 1, ATOMIC_ACQ_REL, ATOMIC_RELAXED)) {
				memcpy(buf->data, element, rb->element_size);
				atomic_store_explicit(&buf->seq, pos + 1, ATOMIC_RELEASE);
				return 0;
			}
		} else if (diff < 0) {
			switch (rb->mode) {
			case RINGBUFFER_OVERWRITE:
				ringbuffer_dequeue(rb, NULL);
				break;
			case RINGBUFFER_BOUNDED:
				return -ENOSPC;
			case RINGBUFFER_DROP_NEW:
				break;
			}
		}
		cpu_relax();
	}
}

int ringbuffer_dequeue(struct ringbuffer* rb, void* element) {
	while (1) {
		size_t pos = atomic_load_explicit(&rb->tail, ATOMIC_RELAXED);
		struct rb_slot* buf = rb_slot_at(rb, pos);
		size_t seq = atomic_load_explicit(&buf->seq, ATOMIC_ACQUIRE);

		ssize_t diff = (ssize_t)seq - (ssize_t)pos;
		if (diff == 1) {
			if (atomic_compare_exchange_weak_explicit(&rb->tail, &pos, pos + 1, ATOMIC_ACQ_REL, ATOMIC_RELAXED)) {
				if (element)
					memcpy(element, buf->data, rb->element_size);
				atomic_store_explicit(&buf->seq, pos + rb->capacity, ATOMIC_RELEASE);
				return 0;
			}
		} else if (diff < 1) {
			return -ENODATA;
		}
		cpu_relax();
	}
}

void ringbuffer_destroy(struct ringbuffer* rb) {
	kfree(rb->buffer);
	rb->buffer = NULL;
}
