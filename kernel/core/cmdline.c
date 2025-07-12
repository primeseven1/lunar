#include <crescent/common.h>
#include <crescent/core/cmdline.h>
#include <crescent/core/limine.h>
#include <crescent/core/printk.h>
#include <crescent/mm/vmm.h>
#include <crescent/lib/hashtable.h>
#include <crescent/lib/string.h>

static struct hashtable* cmdline_hashtable = NULL;

const char* cmdline_get(const char* arg) {
	if (!cmdline_hashtable)
		return NULL;

	const char* ret;
	int err = hashtable_search(cmdline_hashtable, arg, strlen(arg), &ret);
	if (err)
		return NULL;
	return ret;
}

int cmdline_parse(void) {
	const struct limine_executable_file_response* response = g_limine_executable_file_request.response;
	if (!response)
		return -ENOPROTOOPT;
	const char* cmdline = response->executable_file->string;
	if (!cmdline)
		return -ENOPROTOOPT;

	size_t cmdline_size = strlen(cmdline);
	if (cmdline_size == 0)
		return 0;
	cmdline_size++;

	cmdline_hashtable = hashtable_create(5, sizeof(char*));
	if (!cmdline_hashtable)
		return -ENOMEM;

	/* 
	 * Don't want to modify the cmdline directly, so make a copy of it. Use vmap over kmalloc
	 * since after we're done parsing, the memory becomes read only.
	 */
	char* cmdline_copy = vmap(NULL, cmdline_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	char* const cmdline_copy_copy = cmdline_copy;
	if (!cmdline_copy)
		return -ENOMEM;
	strcpy(cmdline_copy, cmdline);

	const char* key;
	const char* value;

	int err = 0;
	while (*cmdline_copy) {
		while (*cmdline_copy == ' ')
			cmdline_copy++;
		if (*cmdline_copy == '\0')
			goto leave;

		/* Parse the option part of the argument */
		key = cmdline_copy;
		while (*cmdline_copy != '=') {
			if (*cmdline_copy == '\0')
				goto leave;
			cmdline_copy++;
		}

		/* Replace the equals sign with a null terminator */
		*cmdline_copy = '\0';
		value = ++cmdline_copy;

		/* Now parse the value of the option we want */
		while (*cmdline_copy != ' ') {
			if (*cmdline_copy == '\0') {
				/* Just insert the last option and then exit */
				err = hashtable_insert(cmdline_hashtable, key, strlen(key), &value);
				if (err)
					goto leave;
				goto leave;
			}
			cmdline_copy++;
		}

		/* Replace the space with a null character, and then insert the value into the hashtable */
		*cmdline_copy = '\0';
		err = hashtable_insert(cmdline_hashtable, key, strlen(key), &value);
		if (err)
			goto leave;

		cmdline_copy++;
	}

	int kprotect_err;
leave:
	kprotect_err = vprotect(cmdline_copy_copy, cmdline_size, MMU_READ, 0);
	if (kprotect_err)
		printk(PRINTK_ERR "core: Failed to remap command line arguments as read only!\n");
	return err;
}
