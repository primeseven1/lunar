#pragma once

#include <lunar/types.h>
#include <arch/asm/errno.h>

#define RINGBUFFER_SIZE_MINIMUM 8

struct ringbuffer {
	void* buffer;
	size_t write, read;
	size_t size;
};

static inline size_t ringbuffer_datacount(struct ringbuffer* rb) {
	return (rb->write - rb->read) & (rb->size - 1);
}

static inline size_t ringbuffer_freespace(struct ringbuffer* rb) {
	return rb->size - ringbuffer_datacount(rb) - 1;
}

/**
 * @brief Initialize a ringbuffer
 *
 * The size is rounded to a power of two. The size must at least
 * be RINGBUFFER_SIZE_MINIMUM, otherwise -EINVAL is returned.
 *
 * The buffer is allocated using kmalloc(). If the size of the ringbuffer
 * is more than a couple pages, vmalloc() is used instead.
 *
 * @param rb The ringbuffer to initialize
 * @param size The size of the ringbuffer.
 *
 * @retval -ENOMEM Out of memory
 * @reval -EINVAL Bad size
 * @retval 0 Success
 */
int ringbuffer_init(struct ringbuffer* rb, size_t size);

/**
 * @brief Free the resources of a ringbuffer
 */
void ringbuffer_destroy(struct ringbuffer* rb);

/**
 * @brief Read from a ringbuffer
 *
 * If `dest` is NULL, data is discarded
 *
 * @param rb The ringbuffer to read from
 * @param dest Where to copy the data to
 * @param count The maximum number of bytes to copy
 *
 * @return The number of bytes read
 */
size_t ringbuffer_read(struct ringbuffer* rb, void* dest, size_t count);

/**
 * @brief Read from a ringbuffer with a user provided cursor
 *
 * @param rb The ringbuffer to read from
 * @param dest Where to copy the data to
 * @param offset Offset into the ringbuffer
 * @param count The maximum number of bytes to read
 *
 * @return The number of bytes read
 */
size_t ringbuffer_peek(struct ringbuffer* rb, void* dest, size_t offset, size_t count);

/**
 * @brief Write data to a ringbuffer
 *
 * @param rb The ringbuffer to write to
 * @param src Where to copy from
 * @param count The maximum number of bytes to copy
 *
 * @return The number of bytes written
 */
size_t ringbuffer_write(struct ringbuffer* rb, const void* src, size_t count);

struct tringbuffer {
	struct ringbuffer rb;
	size_t esize;
};

static inline int tringbuffer_init(struct tringbuffer* trb, size_t ecount, size_t esize) {
	trb->esize = esize;
	return ringbuffer_init(&trb->rb, ecount * esize);
}

static inline size_t tringbuffer_read(struct tringbuffer* trb, void* dest, size_t count) {
	return ringbuffer_read(&trb->rb, dest, count * trb->esize) / trb->esize;	
}

static inline size_t tringbuffer_peek(struct tringbuffer* trb, void* dest, size_t offset, size_t count) {
	return ringbuffer_peek(&trb->rb, dest, offset * trb->esize, count * trb->esize) / trb->esize;
}

static inline size_t tringbuffer_write(struct tringbuffer* trb, const void* src, size_t count) {
	return ringbuffer_write(&trb->rb, src, count * trb->esize) / trb->esize;
}
