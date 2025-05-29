#include <crescent/common.h>
#include <crescent/core/printk.h>
#include <crescent/core/module.h>
#include <crescent/lib/string.h>

extern const struct module _ld_kernel_modules_start;
extern const struct module _ld_kernel_modules_end;

static const struct module* find_builtin_module(const char* name) {
	const struct module* ret = NULL;

	const struct module* mod = &_ld_kernel_modules_start;
	while (mod < &_ld_kernel_modules_end) {
		if (strcmp(name, mod->name) == 0) {
			ret = mod;
			break;
		}

		mod++;
	}

	return ret;
}

int module_load(const char* name) {
	const struct module* mod = find_builtin_module(name);
	if (!mod)
		return -ENOENT;

	if (!mod->init) {
		printk(PRINTK_ERR "core: %s module failed to load, no init function!\n", name);
		return -EFAULT;
	}

	int err = mod->init();

	if (err)
		printk(PRINTK_ERR "core: %s module failed to load, err: %i\n", name, err);
	else
		printk(PRINTK_INFO "core: %s module loaded successfully\n", name);
	return err;
}
