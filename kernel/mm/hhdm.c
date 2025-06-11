#include <crescent/common.h>
#include <crescent/core/limine.h>
#include "hhdm.h"

static volatile struct limine_hhdm_request __limine_request hhdm_request = {
	.request.id = LIMINE_HHDM_REQUEST,
	.request.revision = 0,
	.response = NULL
};

physaddr_t hhdm_physical(void* virtual) {
	const struct limine_hhdm_response* hhdm = hhdm_request.response;
	return (uintptr_t)virtual - hhdm->offset;
}

void* hhdm_virtual(physaddr_t physical) {
	const struct limine_hhdm_response* hhdm = hhdm_request.response;
#ifdef CONFIG_UBSAN
	/* Gets the base of HHDM */
	if (physical == 0)
		return (void*)hhdm->offset;
#endif /* CONFIG_UBSAN */
	return (u8*)physical + hhdm->offset;
}
