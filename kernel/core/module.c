#include <lunar/common.h>
#include <lunar/compiler.h>
#include <lunar/core/printk.h>
#include <lunar/core/module.h>
#include <lunar/lib/string.h>

extern const struct module _ld_kernel_modules_start[];
extern const struct module _ld_kernel_modules_end[];

static const struct module* find_builtin_module(const char* name) {
	const struct module* const start = _ld_kernel_modules_start;
	const struct module* const end = _ld_kernel_modules_end;

	unsigned long mod_count = ((uintptr_t)end - (uintptr_t)start) / sizeof(struct module);
	for (unsigned long i = 0; i < mod_count; i++) {
		const struct module* mod = &_ld_kernel_modules_start[i];
		if (strcmp(name, mod->name) == 0)
			return mod;
	}

	return NULL;
}

int module_load(const char* name) {
	const struct module* mod = find_builtin_module(name);
	if (!mod)
		return -ENOENT;

	/* This shouldn't really happen unless there is some sort of programming error */
	if (unlikely(mod->init_status < init_status_get()))
		return -EAGAIN;

	if (!mod->init) {
		printk(PRINTK_ERR "core: %s module failed to load, no init function!\n", name);
		return -EFAULT;
	}

	int err = mod->init();
	if (!err)
		printk(PRINTK_INFO "core: %s module loaded successfully\n", name);
	return err;
}
