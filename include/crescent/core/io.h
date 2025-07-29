#pragma once

#include <crescent/compiler.h>
#include <crescent/types.h>

/**
 * @brief Write an 8-bit value to an I/O port
 * @param port The I/O port to write to
 * @param val The 8-bit value to write
 */
static inline void outb(u16 port, u8 val) {
	__asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Write a 16-bit value to an I/O port
 * @param port The I/O port to write to
 * @param val The 16-bit value to write
 */
static inline void outw(u16 port, u16 val) {
	__asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Write a 32-bit value to an I/O port
 * @param port The I/O port to write to
 * @param val The 32-bit value to write
 */
static inline void outl(u16 port, u32 val) {
	__asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * @brief Read an 8-bit value from and I/O port
 * @param port The I/O port to read from
 * @return The 8-bit value read from the port
 */
static inline u8 inb(u16 port) {
	u8 ret;
	__asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

/**
 * @brief Read a 16-bit value from and I/O port
 * @param port The I/O port to read from
 * @return The 16-bit value read from the port
 */
static inline u16 inw(u16 port) {
	u16 ret;
	__asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}


/**
 * @brief Read a 32-bit value from and I/O port
 * @param port The I/O port to read from
 * @return The 8-bit value read from the port
 */
static inline u32 inl(u16 port) {
	u32 ret;
	__asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
	return ret;
}

/**
 * @brief Write an 8 bit value to an I/O memory address
 * @param ptr The address to write to
 * @param val The value to write
 */
static inline void writeb(u8 __iomem* ptr, u8 val) {
	*(u8 volatile __force*)ptr = val;
}

/**
 * @brief Write a 16 bit value to an I/O memory address
 * @param ptr The address to write to
 * @param val The value to write
 */
static inline void writew(u16 __iomem* ptr, u16 val) {
	*(u16 volatile __force*)ptr = val;
}

/**
 * @brief Write a 32 bit value to an I/O memory address
 * @param ptr The address to write to
 * @param val The value to write
 */
static inline void writel(u32 __iomem* ptr, u32 val) {
	*(u32 volatile __force*)ptr = val;
}

/**
 * @brief Write a 64 bit value to an I/O memory address
 * @param ptr The address to write to
 * @param val The value to write
 */
static inline void writeq(u64 __iomem* ptr, u64 val) {
	*(u64 volatile __force*)ptr = val;
}

/**
 * @brief Read an 8 bit value from an I/O memory address
 * @param ptr The address to read
 * @return The value read from the address
 */
static inline u8 readb(const u8 __iomem* ptr) {
	return *(u8 volatile __force*)ptr;
}

/**
 * @brief Read a 16 bit value from an I/O memory address
 * @param ptr The address to read
 * @return The value read from the address
 */
static inline u16 readw(const u16 __iomem* ptr) {
	return *(u16 volatile __force*)ptr;
}

/**
 * @brief Read a 32 bit value from an I/O memory address
 * @param ptr The address to read
 * @return The value read from the address
 */
static inline u32 readl(const u32 __iomem* ptr) {
	return *(u32 volatile __force*)ptr;
}

/**
 * @brief Read a 64 bit value from an I/O memory address
 * @param ptr The address to read
 * @return The value read from the address
 */
static inline u64 readq(const u64 __iomem* ptr) {
	return *(u64 volatile __force*)ptr;
}

/**
 * @brief Wait for an I/O operation to complete
 *
 * This function writes to an unused I/O port (0x80) to introduce a small delay,
 * allowing an I/O operation to complete.
 */
static inline void io_wait(void) {
	outb(0x80, 0);
}

/**
 * @brief Set a block of I/O memory to a specified value
 *
 * This function only writes 8 bit values at a time
 *
 * @param val The value to write
 * @param count The number of bytes to write
 */
void __iomem* memset_io(void __iomem* dest, int val, size_t count);

/**
 * @brief Copy a block of memory from one location to another
 *
 * This function only writes 8 bits at a time
 *
 * @param dest Where to copy the block to
 * @param src Where to copy the block from
 * @param count The number of bytes to write
 */
void __iomem* memmove_io(void __iomem* dest, const void __iomem* src, size_t count);
