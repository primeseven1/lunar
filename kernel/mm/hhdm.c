#include <lunar/common.h>
#include <lunar/core/limine.h>
#include <lunar/mm/hhdm.h>

static volatile struct limine_hhdm_request __limine_request hhdm_request = {
	.request.id = LIMINE_HHDM_REQUEST,
	.request.revision = 0,
	.response = NULL
};

physaddr_t hhdm_physical(const void* virtual) {
	const struct limine_hhdm_response* hhdm = hhdm_request.response;
	return (uintptr_t)virtual - hhdm->offset;
}

void* hhdm_virtual(physaddr_t physical) {
	if (!physical)
		return NULL;

	const struct limine_hhdm_response* hhdm = hhdm_request.response;
	return (u8*)physical + hhdm->offset;
}

void* hhdm_base(void) {
	return (void*)hhdm_request.response->offset;
}
