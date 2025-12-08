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
#include <lunar/mm/slab.h>

#include <uacpi/status.h>
#include <uacpi/kernel_api.h>

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
	const char* _level = NULL;
	switch (level) {
	case UACPI_LOG_DEBUG:
	case UACPI_LOG_TRACE:
		_level = PRINTK_DBG;
		break;
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
	if (IS_PTR_ERR(virtual))
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

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle* out) {
	struct pci_device* device;
	int err = pci_device_open(address.bus, address.device, address.function, &device);
	if (err)
		return err == -ENOSYS ? UACPI_STATUS_UNIMPLEMENTED : UACPI_STATUS_INTERNAL_ERROR;

	*out = device;
	return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
	int err = pci_device_close((struct pci_device*)handle);
	if (unlikely(err))
		printk(PRINTK_ERR "acpi: pci_device_close failed with code %i\n", err);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8* value) {
	*value = pci_read_config_byte((struct pci_device*)device, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16* value) {
	*value = pci_read_config_word((struct pci_device*)device, offset);
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32* value) {
	*value = pci_read_config_dword((struct pci_device*)device, offset);
	return UACPI_STATUS_OK;
}

static uacpi_status uacpi_kernel_pci_write(uacpi_handle device, uacpi_size offset, u32 value, size_t count) {
	int err;
	switch (count) {
	case sizeof(u8):
		err = pci_write_config_byte((struct pci_device*)device, offset, (u8)value);
		break;
	case sizeof(u16):
		err = pci_write_config_word((struct pci_device*)device, offset, (u16)value);
		break;
	case sizeof(u32):
		err = pci_write_config_dword((struct pci_device*)device, offset, (u32)value);
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
	struct timespec ts = timekeeper_time(TIMEKEEPER_FROMBOOT);
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
	irqflags_t flags;
	spinlock_t* lock = handle;
	spinlock_lock_irq_save(lock, &flags);
	return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
	spinlock_unlock_irq_restore((spinlock_t*)handle, &flags);
}

static_assert(sizeof(uacpi_cpu_flags) == sizeof(irqflags_t), "sizeof(uacpi_cpu_flags) == sizeof(unsigned long)");

struct uacpi_work {
	uacpi_work_type type;
	uacpi_work_handler handler;
	uacpi_handle ctx;
	struct list_node link;
};

static atomic(size_t) gpe_work_counter = atomic_init(0);
static atomic(size_t) notify_work_counter = atomic_init(0);
static struct slab_cache* work_cache = NULL;

static void uacpi_work(void* arg) {
	struct uacpi_work* work = arg;
	work->handler(work->ctx);
	if (work->type == UACPI_WORK_GPE_EXECUTION)
		atomic_sub_fetch(&gpe_work_counter, 1);
	else
		atomic_sub_fetch(&notify_work_counter, 1);
	slab_cache_free(work_cache, work);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx) {
	struct uacpi_work* work = slab_cache_alloc(work_cache);
	if (!work)
		return UACPI_STATUS_OUT_OF_MEMORY;

	work->type = type;
	work->handler = handler;
	work->ctx = ctx;

	/* GPE's run on the BSP to account for buggy firmware */
	struct cpu* target_cpu = NULL;
	if (type == UACPI_WORK_GPE_EXECUTION) {
		target_cpu = smp_cpus_get()->cpus[0];
		atomic_add_fetch(&gpe_work_counter, 1);
	} else {
		atomic_add_fetch(&notify_work_counter, 1);
	}

	int err = target_cpu ? sched_workqueue_add_on(target_cpu, uacpi_work, work) : sched_workqueue_add(uacpi_work, work);
	if (err) {
		if (type == UACPI_WORK_GPE_EXECUTION)
			atomic_sub_fetch(&gpe_work_counter, 1);
		else
			atomic_sub_fetch(&notify_work_counter, 1);
		slab_cache_free(work_cache, work);
		return UACPI_STATUS_INTERNAL_ERROR;
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
	case UACPI_INIT_LEVEL_SUBSYSTEM_INITIALIZED:
		work_cache = slab_cache_create(sizeof(struct uacpi_work), _Alignof(struct uacpi_work), MM_ZONE_NORMAL | MM_ATOMIC, NULL, NULL);
		if (!work_cache)
			return UACPI_STATUS_OUT_OF_MEMORY;
		break;
	default:
		break;
	}

	return UACPI_STATUS_OK;
}

void uacpi_kernel_deinitialize(void) {
	uacpi_kernel_wait_for_work_completion();

	struct list_node* pos, *tmp;
	list_for_each_safe(pos, tmp, &uacpi_interrupts) {
		struct uacpi_irq_ctx* ctx = list_entry(pos, struct uacpi_irq_ctx, link);
		interrupt_unregister(ctx->isr);
		interrupt_free(ctx->isr);
		list_remove(pos);
	}

	if (work_cache)
		slab_cache_destroy(work_cache);
	work_cache = NULL;
}
