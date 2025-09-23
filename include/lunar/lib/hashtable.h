#pragma once

#include <lunar/types.h>
#include <lunar/core/mutex.h>

struct hashtable_node {
	void* key;
	void* value;
	struct hashtable_node* next;
};

struct hashtable {
	struct hashtable_node** heads;
	size_t head_count;
	size_t size;
	size_t value_size;
	mutex_t lock;
};

/**
 * @brief Create a general-purpose hashtable
 *
 * The structure of the hashtable does not care about types, and in order to avoid
 * collisions, it uses separate chaining. Since this implementation does implement rehashing,
 * it's important that you balance the head count and memory usage.
 *
 * @param head_count The number of heads the linked list should use
 * @param value_size The size of the values
 *
 * @return A pointer to the new hashtable, NULL if memory could not be allocated
 */
struct hashtable* hashtable_create(size_t head_count, size_t value_size);

/**
 * @brief Insert a value into a hash table
 *
 * If the key already exists in the hashtable, this function will replace
 * the value there.
 *
 * @param table The hashtable to use
 * @param key The key to use. This is copied over
 * @param value The value to use, this is also copied over
 *
 * @retval 0 Succesful
 * @retval -ENOMEM Cannot allocate memory for new value
 */
int hashtable_insert(struct hashtable* table, const void* key, size_t key_size, const void* value);

/**
 * @brief Search for a value in a hash table
 *
 * @param[in] table The table to search in
 * @param[in] key The key to use
 * @param[out] value The pointer to where the value will be copied
 *
 * @retval 0 Successful
 * @retval -ENOENT Key not found
 */
int hashtable_search(struct hashtable* table, const void* key, size_t key_size, void* value);

/**
 * @brief Remove a value from a hashtable
 *
 * @param table The table to remove the value from
 * @param key The key to use
 *
 * @retval 0 Successful
 * @retval -ENOENT Key not found
 */
int hashtable_remove(struct hashtable* table, const void* key, size_t key_size);

/**
 * @brief Destroy a hashtable
 *
 * This function will free everything that's related 
 * to the hash table
 *
 * @param table The table to destroy
 */
void hashtable_destroy(struct hashtable* table);
