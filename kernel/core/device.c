#include <lunar/device.h>
#include <lunar/init.h>
#include <lunar/hashtable.h>
#include <lunar/panic.h>
#include <lunar/mm.h>

static struct hashtable* chrdev_tbl;
static MUTEX_DEFINE(chrdev_mtx);

int chrdev_register(struct chrdev* chrdev) {
	int key[] = { chrdev->major, chrdev->minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* tmp;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &tmp);
	if (err == 0) {
		err = -EEXIST;
	} else {
		atomic_store(&chrdev->dead, false);
		err = hashtable_insert(chrdev_tbl, &key, sizeof(key), &chrdev);
		if (err == 0)
			chrdev_hold(chrdev);
	}

	mutex_release(&chrdev_mtx);
	return err;
}

int chrdev_unregister(struct chrdev* chrdev) {
	int key[] = { chrdev->major, chrdev->minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* tmp;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &tmp);
	if (err == 0 && chrdev == tmp) {
		bug(hashtable_remove(chrdev_tbl, &key, sizeof(key)) != 0);
		atomic_store(&chrdev->dead, true);
	} else {
		err = -ENODEV;
	}

	mutex_release(&chrdev_mtx);

	if (err == 0)
		chrdev_release(chrdev);
	return err;
}

int chrdev_get(int major, int minor, struct chrdev** out) {
	if (!out)
		return -EINVAL;

	int key[] = { major, minor };
	mutex_acquire(&chrdev_mtx);

	struct chrdev* chrdev;
	int err = hashtable_search(chrdev_tbl, &key, sizeof(key), &chrdev);
	if (err == 0) {
		chrdev_hold(chrdev);
		*out = chrdev;
	} else {
		err = -ENXIO;
	}

	mutex_release(&chrdev_mtx);
	return err;
}

static void device_init(void) {
	chrdev_tbl = hashtable_create(64, sizeof(struct chrdev*));
	if (!chrdev_tbl)
		out_of_memory();
}

INIT_TASK_DECLARE(heap_init_task);
INIT_TASK_DEFINE(device_init_task, INIT_TASK_SCOPE_BSP, device_init, &heap_init_task);
