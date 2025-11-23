#pragma once

#include <lunar/types.h>
#include <lunar/compiler.h>

/**
 * @brief Set a block of memory to an 8 bit value in user space
 *
 * @param dest The destination pointer
 * @param val The value to set
 * @param count The number of bytes to set
 *
 * @retval -EFAULT Bad user pointer
 * @retval 0 Successful
 */
int usercopy_memset(void __user* dest, int val, size_t count);

/**
 * @brief Copy a block of memory from user space
 *
 * @param dest The destination pointer (kernel)
 * @param src The source pointer (user)
 * @param count The number of bytes to copy
 *
 * @retval -EFAULT Bad user pointer
 * @retval 0 Successful
 */
int usercopy_from_user(void* dest, void __user* src, size_t count);

/**
 * @brief Copy a block from kernel space to user space
 *
 * @param dest The destination pointer (user)
 * @param src The source pointer (kernel)
 * @param count The number of bytes to copy
 *
 * @retval -EFAULT Bad user pointer
 * @retval 0 Successful
 */
int usercopy_to_user(void __user* dest, void* src, size_t count);

/**
 * @brief Get the length of a string from userspace
 * @param str The string
 * @return -EFAULT on bad pointer, or the length of the string
 */
ssize_t usercopy_strlen(const char __user* str);
