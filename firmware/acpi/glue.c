#include <lunar/common.h>
#include <lunar/asm/wrap.h>
#include <lunar/core/printk.h>
#include <lunar/core/limine.h>
#include <lunar/core/pci.h>
#include <lunar/core/apic.h>
#include <lunar/core/io.h>
#include <lunar/core/spinlock.h>
#include <lunar/core/mutex.h>
#include <lunar/core/semaphore.h>
#include <lunar/core/timekeeper.h>
#include <lunar/sched/scheduler.h>
#include <lunar/sched/kthread.h>
#include <lunar/lib/string.h>
#include <lunar/mm/heap.h>
#include <lunar/mm/vmm.h>

#include <uacpi/status.h>
#include <uacpi/kernel_api.h>

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
	const char* _level = NULL;
	switch (level) {
	case UACPI_LOG_DEBUG:
		_level = PRINTK_DBG;
		break;
	case UACPI_LOG_TRACE:
	case UACPI_LOG_INFO:
		_level = PRINTK_INFO;
		break;
	case UACPI_LOG_WARN:
		_level = PRINTK_WARN;
		break;
	case UACPI_LOG_ERROR:
		_level = PRINTK_ERR;
		break;
	}
	printk("%sacpi: %s", _level, str);
}

void* uacpi_kernel_map(uacpi_phys_addr physical, uacpi_size size) {
	size_t page_offset = physical % PAGE_SIZE;
	physaddr_t _physical = physical - page_offset;
	void* virtual = vmap(NULL, size + page_offset, MMU_READ | MMU_WRITE, VMM_PHYSICAL, &_physical);
	if (!virtual)
		return NULL;
	return (u8*)virtual + page_offset;
}

void uacpi_kernel_unmap(void* virtual, uacpi_size size) {
	size_t page_offset = (uintptr_t)virtual % PAGE_SIZE;
	void* _virtual = (u8*)virtual - page_offset;
	int err = vunmap(_virtual, size + page_offset, 0);
	if (err)
		printk(PRINTK_ERR "acpi: %s failed with error code %i\n", __func__, err);
}

static struct limine_rsdp_request __limine_request rsdp_request = {
	.request.id = LIMINE_RSDP_REQUEST,
	.request.revision = 0,
	.response = NULL
};

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr* out) {
	if (!rsdp_request.response)
		return UACPI_STATUS_NOT_FOUND;

	*out = rsdp_request.response->physical;
	return UACPI_STATUS_OK;
}

struct uacpi_pci_device_handle {
	struct pci_device* device;
	uacpi_u8 function;
};

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out) {
	struct uacpi_pci_device_handle* handle = kmalloc(sizeof(*handle), MM_ZONE_NORMAL);
	if (!handle)
		return UACPI_STATUS_OUT_OF_MEMORY;
	struct pci_device* device;
	int err = pci_device_open(address.bus, address.device, &device);
	if (err) {
		kfree(handle);
		return err == -ENOSYS ? UACPI_STATUS_UNIMPLEMENTED : UACPI_STATUS_INTERNAL_ERROR;
	}

	handle->device = device;
	handle->function = address.function;
	*out = handle;

	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
	struct uacpi_pci_device_handle* uacpi_device = handle;
	int err = pci_device_close(uacpi_device->device);
	if (unlikely(err))
		printk(PRINTK_ERR "acpi: pci_device_close failed with code %i\n", err);

	kfree(uacpi_device);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
	struct uacpi_pci_device_handle* uacpi_device = device;
	*value = pci_read_config_byte(uacpi_device->device, uacpi_device->function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
	struct uacpi_pci_device_handle* uacpi_device = device;
	*value = pci_read_config_word(uacpi_device->device, uacpi_device->function, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
	struct uacpi_pci_device_handle* uacpi_device = device;
	*value = pci_read_config_dword(uacpi_device->device, uacpi_device->function, offset);
	return UACPI_STATUS_OK;
}

static uacpi_status uacpi_kernel_pci_write(uacpi_handle device, uacpi_size offset, u32 value, size_t count) {
	struct uacpi_pci_device_handle* uacpi_device = device;
	int err;
	switch (count) {
	case sizeof(u8):
		err = pci_write_config_byte(uacpi_device->device, uacpi_device->function, offset, (u8)value);
		break;
	case sizeof(u16):
		err = pci_write_config_word(uacpi_device->device, uacpi_device->function, offset, (u16)value);
		break;
	case sizeof(u32):
		err = pci_write_config_dword(uacpi_device->device, uacpi_device->function, offset, (u32)value);
		break;
	default:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	switch (err) {
	case -ENOSYS:
		return UACPI_STATUS_UNIMPLEMENTED;
	case -ENODEV:
		return UACPI_STATUS_INTERNAL_ERROR;
	case -EINVAL:
		return UACPI_STATUS_INVALID_ARGUMENT;
	}

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	return uacpi_kernel_pci_write(device, offset, value, sizeof(u8));
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	return uacpi_kernel_pci_write(device, offset, value, sizeof(u16));
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	return uacpi_kernel_pci_write(device, offset, value, sizeof(u32));
}

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out) {
	(void)len;
	*out = (uacpi_handle)base;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
	(void)handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out) {
	*out = inb((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out) {
	*out = inw((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out) {
	*out = inl((uacpi_io_addr)handle + offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value) {
	outb((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value) {
	outw((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value) {
	outl((uacpi_io_addr)handle + offset, value);
	return UACPI_STATUS_OK;
}

void* uacpi_kernel_alloc(uacpi_size size) {
	return kmalloc(size, MM_ZONE_NORMAL);
}

void* uacpi_kernel_alloc_zeroed(uacpi_size size) {
	return kzalloc(size, MM_ZONE_NORMAL);
}

void uacpi_kernel_free(void* ptr) {
	if (ptr)
		kfree(ptr);
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
	struct timespec ts = timekeeper_time();
	time_t ns = ts.tv_sec * 1000000000 + ts.tv_nsec;
	return ns;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
	timekeeper_stall(usec);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
	sched_prepare_sleep(msec, 0);
	schedule();
}

uacpi_handle uacpi_kernel_create_mutex(void) {
	mutex_t* mutex = kmalloc(sizeof(*mutex), MM_ZONE_NORMAL);
	if (mutex)
		mutex_init(mutex);
	return mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
	kfree((mutex_t*)handle);
}

uacpi_handle uacpi_kernel_create_event(void) {
	struct semaphore* sem = kmalloc(sizeof(*sem), MM_ZONE_NORMAL);
	if (sem)
		semaphore_init(sem, 0);
	return sem;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
	kfree((struct semaphore*)handle);
}

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
	return current_thread();
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
	mutex_t* mutex = handle;
	if (timeout == 0xFFFF) {
		mutex_lock(mutex);
		return UACPI_STATUS_OK;
	}

	int err = mutex_lock_timed(mutex, timeout);
	if (err != 0) {
		if (err == -ETIMEDOUT)
			return UACPI_STATUS_TIMEOUT;
		return UACPI_STATUS_INTERNAL_ERROR;
	}

	return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
	mutex_unlock((mutex_t*)handle);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
	struct semaphore* sem = handle;
	if (timeout == 0xFFFF)
		return semaphore_wait(sem, 0) == 0;

	int err = semaphore_wait_timed(sem, timeout, 0);
	return err == 0 ? UACPI_TRUE : UACPI_FALSE;
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
	semaphore_signal((struct semaphore*)handle);
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
	int err = semaphore_reset((struct semaphore*)handle);
	if (err)
		printk(PRINTK_ERR "acpi: Failed to reset semaphore\n");
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* request) {
	switch (request->type) {
	case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
		break;
	case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
		printk(PRINTK_CRIT "acpi: fatal firmware error: type=%d code=%d arg=%ld\n",
				request->fatal.type, request->fatal.code, request->fatal.arg);
		break;
	}
	return UACPI_STATUS_OK;
}

static LIST_HEAD_DEFINE(uacpi_interrupts);
static MUTEX_DEFINE(uacpi_interrupts_lock);

struct uacpi_irq_ctx {
	struct isr* isr;
	uacpi_interrupt_handler handler;
	uacpi_handle ctx;
	struct list_node link;
};

static void uacpi_irq(struct isr* isr, struct context* ctx) {
	(void)ctx;
	struct uacpi_irq_ctx* actx = isr->private;
	actx->handler(actx->ctx);
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx, uacpi_handle* out_handle) {
	struct uacpi_irq_ctx* actx = kmalloc(sizeof(*actx), MM_ZONE_NORMAL);
	if (!actx)
		return UACPI_STATUS_OUT_OF_MEMORY;
	struct isr* isr = interrupt_alloc();
	if (!isr) {
		kfree(actx);
		return UACPI_STATUS_INTERNAL_ERROR;
	}
	int err = interrupt_register(isr, uacpi_irq, apic_set_irq, irq, current_cpu(), true);
	if (err) {
		interrupt_free(isr);
		kfree(actx);
		return UACPI_STATUS_INTERNAL_ERROR;
	}

	actx->isr = isr;
	actx->ctx = ctx;
	actx->handler = handler;
	isr->private = actx;

	mutex_lock(&uacpi_interrupts_lock);
	list_node_init(&actx->link);
	list_add(&uacpi_interrupts, &actx->link);
	mutex_unlock(&uacpi_interrupts_lock);

	isr->irq.set_masked(isr, false);
	*out_handle = isr;
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler, uacpi_handle irq_handle) {
	(void)handler;
	struct isr* isr = irq_handle;
	mutex_lock(&uacpi_interrupts_lock);

	struct uacpi_irq_ctx* actx = isr->private;
	if (interrupt_unregister(isr) != 0)
		return UACPI_STATUS_INTERNAL_ERROR;
	list_remove(&actx->link);

	mutex_unlock(&uacpi_interrupts_lock);

	kfree(actx);
	interrupt_free(isr);
	return UACPI_STATUS_OK;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
	spinlock_t* lock = kmalloc(sizeof(*lock), MM_ZONE_NORMAL);
	if (lock)
		spinlock_init(lock);
	return lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
	kfree((spinlock_t*)handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
	unsigned long flags;
	spinlock_t* lock = handle;
	spinlock_lock_irq_save(lock, &flags);
	return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
	spinlock_unlock_irq_restore((spinlock_t*)handle, &flags);
}

static_assert(sizeof(uacpi_cpu_flags) == sizeof(unsigned long), "sizeof(uacpi_cpu_flags) == sizeof(unsigned long)");

struct uacpi_work {
	uacpi_work_type type;
	uacpi_work_handler handler;
	uacpi_handle ctx;
	struct list_node link;
};

static LIST_HEAD_DEFINE(work_free_list);
static SPINLOCK_DEFINE(work_free_list_lock);

/* Usually scheduled in a workqueue */
static void work_list_grow(void* arg) {
	size_t count = 16;
	if (arg)
		count = (size_t)arg;

	struct uacpi_work** pointers = kzalloc(sizeof(*pointers) * count, MM_ZONE_NORMAL);
	for (size_t i = 0; i < count; i++) {
		pointers[i] = kzalloc(sizeof(pointers[0]), MM_ZONE_NORMAL);
		if (!pointers[i])
			break;
		list_node_init(&pointers[i]->link);
	}

	unsigned long irq_flags;
	spinlock_lock_irq_save(&work_free_list_lock, &irq_flags);

	for (size_t i = 0; i < count && pointers[i]; i++)
		list_add(&work_free_list, &pointers[i]->link);

	spinlock_unlock_irq_restore(&work_free_list_lock, &irq_flags);
}

static struct uacpi_work* alloc_work_atomic(void) {
	unsigned long irq_flags;
	spinlock_lock_irq_save(&work_free_list_lock, &irq_flags);

	struct uacpi_work* work = NULL;
	if (!list_empty(&work_free_list)) {
		work = list_first_entry(&work_free_list, struct uacpi_work, link);
		list_remove(&work->link);
	}

	spinlock_unlock_irq_restore(&work_free_list_lock, &irq_flags);
	return work;
}

static void free_work_atomic(struct uacpi_work* work) {
	unsigned long irq_flags;
	spinlock_lock_irq_save(&work_free_list_lock, &irq_flags);

	memset(work, 0, sizeof(*work));
	list_add(&work_free_list, &work->link);

	spinlock_unlock_irq_restore(&work_free_list_lock, &irq_flags);
}

static atomic(size_t) gpe_work_counter = atomic_init(0);
static atomic(size_t) notify_work_counter = atomic_init(0);

static void uacpi_work(void* arg) {
	struct uacpi_work* work = arg;
	work->handler(work->ctx);
	if (work->type == UACPI_WORK_GPE_EXECUTION)
		atomic_sub_fetch(&gpe_work_counter, 1);
	else
		atomic_sub_fetch(&notify_work_counter, 1);
	free_work_atomic(work);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
	struct uacpi_work* work = alloc_work_atomic();
	if (!work) {
		sched_workqueue_add(work_list_grow, NULL);
		return UACPI_STATUS_OUT_OF_MEMORY;
	}

	work->type = type;
	work->handler = handler;
	work->ctx = ctx;

	/* GPE's run on the BSP to account for buggy firmware */
	struct cpu* target_cpu;
	if (type == UACPI_WORK_GPE_EXECUTION) {
		target_cpu = smp_cpus_get()->cpus[0];
		atomic_add_fetch(&gpe_work_counter, 1);
	} else {
		target_cpu = current_cpu();
		atomic_add_fetch(&notify_work_counter, 1);
	}

	/* Now enqueue the work to run */
	int err = sched_workqueue_add_on(target_cpu, uacpi_work, work);
	while (err == -EAGAIN) {
		err = sched_workqueue_add_on(target_cpu, uacpi_work, work);
		cpu_relax();
	}

	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
	mutex_lock(&uacpi_interrupts_lock);

	/* First sync and block all interrupts installed by uACPI */
	struct uacpi_irq_ctx* ctx;
	list_for_each_entry(ctx, &uacpi_interrupts, link)
		bug(interrupt_synchronize(ctx->isr) != 0);

	/* Now wait for deferred work to finish */
	while (atomic_load(&gpe_work_counter))
		schedule();
	while (atomic_load(&notify_work_counter))
		schedule();

	/* Allow interrupts to run again */
	list_for_each_entry(ctx, &uacpi_interrupts, link)
		bug(interrupt_allow_entry_if_synced(ctx->isr) != 0);

	mutex_unlock(&uacpi_interrupts_lock);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_initialize(uacpi_init_level current_init_lvl) {
	switch (current_init_lvl) {
	case UACPI_INIT_LEVEL_EARLY:
		break;
	case UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED:
		work_list_grow((void*)64);
		break;
	case UACPI_INIT_LEVEL_NAMESPACE_LOADED:
		break;
	case UACPI_INIT_LEVEL_NAMESPACE_INITIALIZED:
		break;
	}

	return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize(void) {
}
