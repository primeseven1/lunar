#include <crescent/common.h>
#include <crescent/core/limine.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/printk.h>
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
	printk("%s acpi: %s", _level, str);
}

void* uacpi_kernel_map(uacpi_phys_addr physical, size_t size) {
	size_t page_offset = (uintptr_t)physical & (PAGE_SIZE - 1);
	void* virtual = (void*)iomap(physical, ROUND_UP(size + page_offset, PAGE_SIZE), MMU_READ | MMU_WRITE);
	if (!virtual)
		return NULL;
	return (u8*)virtual + page_offset;
}

void uacpi_kernel_unmap(void* virtual, size_t size) {
	size_t page_offset = (uintptr_t)virtual & (PAGE_SIZE - 1);
	uintptr_t _virtual = (uintptr_t)virtual;
	iounmap((void*)ROUND_DOWN(_virtual, PAGE_SIZE), ROUND_UP(size + page_offset, PAGE_SIZE));
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
