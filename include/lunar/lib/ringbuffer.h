#pragma once

#include <lunar/asm/errno.h>
#include <lunar/core/spinlock.h>
#include <lunar/mm/heap.h>

struct rb_slot {
	atomic(size_t) seq;
	__attribute__((aligned(BIGGEST_ALIGNMENT))) u8 data[];
};

enum ringbuffer_modes {
	RINGBUFFER_OVERWRITE,
	RINGBUFFER_BOUNDED,
	RINGBUFFER_DROP_NEW,
	__RINGBUFFER_MODE_MAX = RINGBUFFER_DROP_NEW,
};

struct ringbuffer {
	struct rb_slot* buffer;
	unsigned int mode;
	size_t capacity;
	size_t element_size;
	atomic(size_t) head;
	atomic(size_t) tail;
};

/**
 * @brief Initialize a ringbuffer structure
 *
 * @param rb The ringbuffer to initialize
 * @param mode How the ringbuffer should operate when full
 * @param capacity The capacity of the ringbuffer, must be a power of two
 * @param element_size The size of the elements in the ringbuffer
 *
 * @retval -EINVAL capacity is zero or not a power of two, or the mode is invalid
 * @retval -ENOMEM Failed to allocate memory for rb->buffer
 * @retval 0 Successful
 */
int ringbuffer_init(struct ringbuffer* rb, unsigned int mode, size_t capacity, size_t element_size);

/**
 * @brief Free rb->buffer 
 */
static inline void ringbuffer_free(struct ringbuffer* rb) {
	kfree(rb->buffer);
}

/**
 * @brief Add an element to a ringbuffer
 *
 * @param rb The ringbuffer to use
 * @param element The element to put in the ringbuffer
 * 
 * @return -ENOSPC when the ringbuffer is out of room, 0 on success
 */
int ringbuffer_enqueue(struct ringbuffer* rb, const void* element);

/**
 * @brief Remove an element from a ringbuffer and pop it into `element`
 *
 * @param rb The ringbuffer
 * @param element Where to store the popped element
 *
 * @return -ENODATA when there is nothing in the ringbuffer, 0 on success
 */
int ringbuffer_dequeue(struct ringbuffer* rb, void* element);

/**
 * @brief Free any resources used by the ringbuffer
 * @param rb The ringbuffer to destroy
 */
void ringbuffer_destroy(struct ringbuffer* rb);
