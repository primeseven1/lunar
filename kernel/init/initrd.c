#include <lunar/core/limine.h>
#include <lunar/core/abi.h>
#include <lunar/core/panic.h>
#include <lunar/core/vfs.h>
#include <lunar/core/printk.h>
#include <lunar/lib/string.h>
#include <lunar/lib/convert.h>
#include "internal.h"

struct ustar_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char modtime[12];
	char checksum[8];
	char type_flag[1];
	char link[100];
	char indicator[6];
	char version[2];
	char uname[32];
	char gname[32];
	char dev_major[8];
	char dev_minor[8];
	char prefix[155];
};

struct ustar_entry {
	char name[PATHNAME_MAX + 1];
	mode_t mode;
	uid_t uid;
	gid_t gid;
	size_t size;
	struct timespec modtime;
	u16 checksum;
	u8 type_flag;
	char link[101];
	char indicator[6];
	char version[2];
	u32 dev_minor;
	u32 dev_major;
};

#define USTAR_BLOCKSIZE 512

enum ustar_types {
	USTAR_FILE,
	USTAR_HARDLINK,
	USTAR_SYMLINK,
	USTAR_CHARDEV,
	USTAR_BLOCK,
	USTAR_DIR,
	USTAR_FIFO
};

static unsigned long long parse_integer(const char* str, size_t len) {
	unsigned long long res = 0;
	while (len--) {
		res *= 8;
		res += *str - '0';
		str++;
	}
	return res;
}

static void build_entry(struct ustar_entry* entry, const void* addr) {
	const struct ustar_header* header = addr;

	entry->name[0] = '\0';
	if (header->prefix[0]) {
		strlcpy(entry->name, header->prefix, sizeof(entry->name));
		strlcat(entry->name, "/", sizeof(entry->name));
	}
	strlcat(entry->name, header->name, sizeof(entry->name));

	entry->mode = parse_integer(header->mode, sizeof(header->mode) - 1);
	entry->uid = parse_integer(header->uid, sizeof(header->uid) - 1);
	entry->gid = parse_integer(header->gid, sizeof(header->gid) - 1);
	entry->size = parse_integer(header->size, sizeof(header->size) - 1);
	entry->modtime.tv_sec = parse_integer(header->modtime, sizeof(header->modtime) - 1);
	entry->modtime.tv_nsec = 0;
	entry->checksum = parse_integer(header->checksum, sizeof(header->checksum) - 1);
	entry->type_flag = parse_integer(header->type_flag, sizeof(header->type_flag));
	entry->dev_major = parse_integer(header->dev_major, sizeof(header->dev_major) - 1);
	entry->dev_minor = parse_integer(header->dev_minor, sizeof(header->dev_minor) - 1);

	strlcpy(entry->link, header->link, sizeof(entry->link));
	memcpy(entry->indicator, header->indicator, sizeof(entry->indicator));
	memcpy(entry->version, header->version, sizeof(entry->version));
}

static void handle_entry(struct ustar_entry* entry, const void* data) {
	int err;
	struct vnode* node;

	const char* function = NULL;
	const char* type = NULL;

	switch (entry->type_flag) {
	case USTAR_FILE:
		type = "USTAR_FILE";
		err = vfs_create(NULL, entry->name, 0, VNODE_TYPE_REGULAR, &node);
		if (err) {
			function = "vfs_create";
		} else {
			err = vfs_write(node, data, entry->size, 0, 0);
			if (err < 0)
				function = "vfs_write";
			vnode_unref(node);
		}
		break;
	case USTAR_DIR:
		type = "USTAR_DIR";
		function = "vfs_create";
		err = vfs_create(NULL, entry->name, 0, VNODE_TYPE_DIR, NULL);
		break;
	default:
		err = -ENOSYS;
		break;
	}

	if (err < 0) {
		if (function)
			printk(PRINTK_ERR "initrd: %s(): %i with %s\n", function, err, type);
		else
			printk(PRINTK_ERR "initrd: Unsupported type %i\n", entry->type_flag);
	}
}

static struct limine_internal_module _initrd_mod = {
	.flags = 0,
	.path = "/initrd",
	.string = "initrd-default"
};
static struct limine_internal_module* initrd_mod[] =  { &_initrd_mod };
static volatile struct limine_module_request __limine_request mod_request = {
	.request.id = LIMINE_MODULE_REQUEST,
	.request.revision = 0,
	.response = NULL,
	.internal_modules = initrd_mod,
	.internal_module_count = ARRAY_SIZE(initrd_mod)
};

static struct limine_file* find_initrd(void) {
	struct limine_module_response* response = mod_request.response;
	if (!response)
		return NULL;

	struct limine_file* initrd = NULL;
	for (u64 i = 0; i < response->module_count; ++i) {
		/* initrd-default can be overridden by a user provided module */
		if (strcmp(response->modules[i]->string, "initrd-default") == 0) {
			initrd = response->modules[i];
		} else if (strcmp(response->modules[i]->string, "initrd") == 0) {
			initrd = response->modules[i];
			break;
		}
	}

	return initrd;
}

void initrd_init(void) {
	struct limine_file* initrd = find_initrd();
	if (!initrd) {
		printk(PRINTK_WARN "initrd: No initrd found\n");
		return;
	}

	struct ustar_entry* entry = kmalloc(sizeof(*entry), MM_ZONE_NORMAL);
	if (unlikely(!entry)) {
		printk("initrd: Out of memory\n");
		return;
	}

	const void* _entry = initrd->address;
	while (1) {
		build_entry(entry, _entry);
		if (strncmp("ustar", entry->indicator, 5) != 0)
			break;

		const void* data = (u8*)_entry + USTAR_BLOCKSIZE;
		_entry = (u8*)data + ROUND_UP(entry->size, USTAR_BLOCKSIZE);

		handle_entry(entry, data);
	}
	kfree(entry);
}
