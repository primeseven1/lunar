#include <crescent/common.h>
#include <crescent/core/limine.h>

/* clang-format off */

/* Tell the bootloader where to start looking for requests */
__attribute__((section(".limine_requests_start_marker"), aligned(8), used))
static u64 start_marker[4] = { 
	0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, 0x785c6ed015d3e316, 0x181e920a7852b9d9
};

#define LIMINE_BASE_REVISION_MAGIC2 0x6a7b384944536bdc

/* 
 * If the requested base revision is supported, the bootloader will set the last element to zero.
 * The bootloader will also set the second element to the real base revision that was used to load the kernel.
 * Since the compiler does not know this, this must be marked as volatile.
 */
static volatile u64 __limine_request base_revision[3] = { 
	0xf9562b2d5c95a6c8, LIMINE_BASE_REVISION_MAGIC2, LIMINE_BASE_REVISION 
};

static struct limine_stack_size_request __limine_request stack_size = {
	.request.id = LIMINE_STACK_SIZE_REQUEST,
	.request.revision = 0,
	.response = NULL,
	.stack_size = 0x4000
};

struct limine_executable_file_request __limine_request g_limine_executable_file_request = {
	.request.id = LIMINE_EXECUTABLE_FILE_REQUEST,
	.request.revision = 0,
	.response = NULL
};

/* Tell the bootloader where to stop searching for requests */
__attribute__((section(".limine_requests_end_marker"), aligned(8), used))
static u64 end_marker[2] = { 0xadc0e0531bb10d03, 0x9572709f31764c62 };

/* clang-format on */

int limine_base_revision(void) {
	if (unlikely(base_revision[1] == LIMINE_BASE_REVISION_MAGIC2))
		return -EINVAL;
	return base_revision[1];
}
