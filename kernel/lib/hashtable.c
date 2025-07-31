#include <crescent/common.h>
#include <crescent/asm/errno.h>
#include <crescent/lib/string.h>
#include <crescent/lib/hashtable.h>
#include <crescent/mm/heap.h>

#define FNV1A64_PRIME 0x00000100000001b3ull
#define FNV1A64_OFFSET 0xcbf29ce484222325ull

static inline u64 fnv1a64_hash(const void* bytes, size_t size) {
	const u8* b = bytes;

	u64 hash = FNV1A64_OFFSET;
	while (size--) {
		hash ^= *b++;
		hash *= FNV1A64_PRIME;
	}
	return hash;
}

struct hashtable* hashtable_create(size_t head_count, size_t value_size) {
	if (head_count == 0)
		return NULL;
	struct hashtable* table = kmalloc(sizeof(*table), MM_ZONE_NORMAL);
	if (!table)
		return NULL;

	table->heads = kzalloc(head_count * sizeof(struct hashtable_node), MM_ZONE_NORMAL);
	if (!table->heads) {
		kfree(table);
		return NULL;
	}

	table->head_count = head_count;
	table->size = 0;
	table->value_size = value_size;
	spinlock_init(&table->lock);

	return table;
}

static struct hashtable_node* 
hashtable_node_create(const void* key, const void* value, size_t key_size, size_t value_size) {
	struct hashtable_node* node = kmalloc(sizeof(struct hashtable_node), MM_ZONE_NORMAL);
	if (!node)
		return NULL;

	node->key = kmalloc(key_size, MM_ZONE_NORMAL);
	if (!node->key) {
		kfree(node);
		return NULL;
	}

	node->value = kmalloc(value_size, MM_ZONE_NORMAL);
	if (!node->value) {
		kfree(node->key);
		kfree(node);
		return NULL;
	}
	node->next = NULL;

	memcpy(node->key, key, key_size);
	memcpy(node->value, value, value_size);

	return node;
}

int hashtable_insert(struct hashtable* table, const void* key, size_t key_size, const void* value) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret;

	unsigned long flags;
	spinlock_lock_irq_save(&table->lock, &flags);

	size_t index = hash % table->head_count;

	struct hashtable_node* node = table->heads[index];
	struct hashtable_node* prev = NULL;

	/* See if the key is already in the table */
	while (node) {
		if (memcmp(node->key, key, key_size) == 0) {
			memcpy(node->value, value, table->value_size);
			ret = 0;
			goto out;
		}

		prev = node;
		node = node->next;
	}

	/* Since there is no matching key, another node needs to be created */
	struct hashtable_node* new = hashtable_node_create(key, value, key_size, table->value_size);
	if (!new) {
		ret = -ENOMEM;
		goto out;
	}

	if (prev)
		prev->next = new;
	else
		table->heads[index] = new;
	table->size++;

	ret = 0;
out:
	spinlock_unlock_irq_restore(&table->lock, &flags);
	return ret;
}

int hashtable_search(struct hashtable* table, const void* key, size_t key_size, void* value) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret;

	unsigned long flags;
	spinlock_lock_irq_save(&table->lock, &flags);

	size_t index = hash % table->head_count;

	const struct hashtable_node* node = table->heads[index];
	while (node) {
		if (memcmp(node->key, key, key_size) == 0) {
			memcpy(value, node->value, table->value_size);
			ret = 0;
			goto out;
		}
		
		node = node->next;
	}

	ret = -ENOENT;
out:
	spinlock_unlock_irq_restore(&table->lock, &flags);
	return ret;
}

int hashtable_remove(struct hashtable* table, const void* key, size_t key_size) {
	u64 hash = fnv1a64_hash(key, key_size);
	int ret;

	unsigned long flags;
	spinlock_lock_irq_save(&table->lock, &flags);

	size_t index = hash % table->head_count;

	struct hashtable_node* node = table->heads[index];
	struct hashtable_node* prev = NULL;

	/* Search for the key in the table */
	while (node) {
		if (memcmp(node->key, key, key_size) == 0) {
			if (prev)
				prev->next = node->next;
			else
				table->heads[index] = node->next;

			kfree(node->key);
			kfree(node->value);
			kfree(node);

			table->size--;
			ret = 0;
			goto out;
		}

		prev = node;
		node = node->next;
	}

	ret = -ENOENT;
out:
	spinlock_unlock_irq_restore(&table->lock, &flags);
	return ret;
}

void hashtable_destroy(struct hashtable* table) {
	for (size_t i = 0; i < table->head_count; i++) {
		struct hashtable_node* node = table->heads[i];
		while (node) {
			struct hashtable_node* next = node->next;
			kfree(node->key);
			kfree(node->value);
			kfree(node);
			node = next;
		}
	}

	kfree(table->heads);
	kfree(table);
}
