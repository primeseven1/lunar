#include <lunar/common.h>
#include <lunar/hashtable.h>
#include <lunar/string.h>
#include <lunar/limine.h>
#include <lunar/vmm.h>
#include <lunar/panic.h>
#include <lunar/printk.h>
#include <lunar/cmdline.h>
#include <lunar/init.h>

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

static void cmdline_init(void) {
	const struct limine_executable_file_response* response = g_limine_executable_file_request.response;
	if (!response)
		return;
	const char* cmdline = response->executable_file->string;
	if (!cmdline)
		return;

	size_t cmdline_size = strlen(cmdline);
	if (cmdline_size++ == 0)
		return;
	if (cmdline_size > PAGE_SIZE)
		cmdline_size = PAGE_SIZE;

	/* Hastable with 10 nodes is probably more than enough */
	cmdline_hashtable = hashtable_create(10, sizeof(char*));
	if (!cmdline_hashtable)
		return;

	/* Create a writable copy, this will be tokenized and made read only */
	struct page* page = page_alloc_page(MM_ZONE_NORMAL);
	if (!page)
		return;
	char* cmdline_copy = vm_map(NULL, &page, 1, PGPROT_READ | PGPROT_WRITE, 0);
	if (IS_PTR_ERR(cmdline_copy))
		return;

	char* const cmdline_base = cmdline_copy;
	strlcpy(cmdline_copy, cmdline, PAGE_SIZE);

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

	printk(PRINTK_INFO "cmdline: %s\n", cmdline);
	err = vm_protect(cmdline_base, 1, PGPROT_READ, 0);
	if (err)
		printk("cmdline: Failed to make command line read only: %d\n", err);
}

INIT_TASK_DECLARE(vmm_init_task, heap_init_task);
INIT_TASK_DEFINE(cmdline_init_task, INIT_TASK_SCOPE_BSP, cmdline_init, &vmm_init_task, &heap_init_task);
