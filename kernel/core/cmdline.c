#include <lunar/common.h>
#include <lunar/core/cmdline.h>
#include <lunar/core/limine.h>
#include <lunar/core/printk.h>
#include <lunar/mm/vmm.h>
#include <lunar/lib/hashtable.h>
#include <lunar/lib/string.h>

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
	if (cmdline_size++ == 0)
		return 0;

	/* Hastable with 10 nodes is probably more than enough */
	cmdline_hashtable = hashtable_create(10, sizeof(char*));
	if (!cmdline_hashtable)
		return -ENOMEM;

	/* Create a writable copy, this will be tokenized and made read only */
	char* cmdline_copy = vmap(NULL, cmdline_size, MMU_READ | MMU_WRITE, VMM_ALLOC, NULL);
	if (IS_PTR_ERR(cmdline_copy))
		return PTR_ERR(cmdline_copy);

	char* const cmdline_base = cmdline_copy;
	strcpy(cmdline_copy, cmdline);

	int err = 0;
	char* save_outer = NULL;
	for (char* tok = strtok_r(cmdline_copy, " ", &save_outer); tok != NULL; tok = strtok_r(NULL, " ", &save_outer)) {
		char* save_inner = NULL;
		char* key = strtok_r(tok, "=", &save_inner);
		char* value = strtok_r(NULL, "=", &save_inner);
		if (!key || !value)
			break;

		err = hashtable_insert(cmdline_hashtable, key, strlen(key), &value);
		if (err)
			break;
	}

	if (unlikely(vprotect(cmdline_base, cmdline_size, MMU_READ, 0, NULL)))
		printk(PRINTK_ERR "core: Failed to remap command line arguments as read only!\n");
	return err;
}
