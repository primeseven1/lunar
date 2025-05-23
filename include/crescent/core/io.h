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
 * @param addr The address to write to
 * @param val The value to write
 */
static inline void writeb(volatile u8 __iomem* addr, u8 val) {
	*(volatile u8*)addr = val;
}

/**
 * @brief Write a 16 bit value to an I/O memory address
 * @param addr The address to write to
 * @param val The value to write
 */
static inline void writew(volatile u16 __iomem* addr, u16 val) {
	*(volatile u16*)addr = val;
}

/**
 * @brief Write a 32 bit value to an I/O memory address
 * @param addr The address to write to
 * @param val The value to write
 */
static inline void writel(volatile u32 __iomem* addr, u32 val) {
	*(volatile u32*)addr = val;
}

/**
 * @brief Write a 64 bit value to an I/O memory address
 * @param addr The address to write to
 * @param val The value to write
 */
static inline void writeq(volatile u64 __iomem* addr, u64 val) {
	*(volatile u64*)addr = val;
}

/**
 * @brief Read an 8 bit value from an I/O memory address
 * @param addr The address to read
 * @return The value read from the address
 */
static inline u8 readb(const volatile u8 __iomem* addr) {
	return *(volatile u8*)addr;
}

/**
 * @brief Read a 16 bit value from an I/O memory address
 * @param addr The address to read
 * @return The value read from the address
 */
static inline u16 readw(const volatile u16 __iomem* addr) {
	return *(volatile u16*)addr;
}

/**
 * @brief Read a 32 bit value from an I/O memory address
 * @param addr The address to read
 * @return The value read from the address
 */
static inline u32 readl(const volatile u32 __iomem* addr) {
	return *(volatile u32*)addr;
}

/**
 * @brief Read a 64 bit value from an I/O memory address
 * @param addr The address to read
 * @return The value read from the address
 */
static inline u64 readq(const volatile u64 __iomem* addr) {
	return *(volatile u64*)addr;
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
