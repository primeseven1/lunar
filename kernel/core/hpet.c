#include <crescent/compiler.h>
#include <crescent/mm/vmm.h>
#include <crescent/core/timekeeper.h>
#include <crescent/core/io.h>
#include <crescent/core/interrupt.h>
#include <crescent/core/apic.h>
#include <crescent/core/cpu.h>
#include <crescent/core/printk.h>

#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#define HPET_TIMER_CONFIG(t) (0x20 + 0x4 * (t))
#define HPET_TIMER_COMPARATOR(t) (0x21 + 0x4 * (t))
#define HPET_TIMER_FSBROUTE(t) (0x22 + 0x4 * (t))

enum hpet_regs {
	HPET_REG_CAPS = 0x00,
	HPET_REG_CONFIG = 0x02,
	HPET_REG_INTSTATUS = 0x04,
	HPET_REG_COUNTER = 0x1e
};

enum hpet_configs {
	HPET_TIMER_CONFIG_LEVEL_TRIGGER = (1ul << 1),
	HPET_TIMER_CONFIG_IRQENABLE = (1ul << 2),
	HPET_TIMER_CONFIG_PERIODIC = (1ul << 3),
	HPET_TIMER_CONFIG_FORCE32BIT = (1ul << 8),
	HPET_TIMER_CONFIG_FSBENABLE = (1ul << 14),
	HPET_TIMER_CONFIG_FSBCAPABLE = (1ul << 15)
};

enum hpet_caps {
	HPET_CAP_LEGACY = (1 << 15)
};

static struct acpi_hpet* hpet;
static u64 __iomem* hpet_virtual;

static inline u64 hpet_read(unsigned int reg) {
	return readq(hpet_virtual + reg);
}

static inline void hpet_write(unsigned int reg, u64 x) {
	writeq(hpet_virtual + reg, x);
}

static time_t get_ticks(void) {
	return hpet_read(HPET_REG_COUNTER);	
}

static struct timekeeper_source _hpet_source;
static struct timekeeper_source* hpet_source = NULL;

static int init(struct timekeeper_source** out) {
	if (hpet_source) {
		*out = hpet_source;
		return 0;
	}

	uacpi_table table;
	uacpi_status status = uacpi_table_find_by_signature("HPET", &table);
	if (status != UACPI_STATUS_OK) {
		if (status == UACPI_STATUS_MAPPING_FAILED || status == UACPI_STATUS_OUT_OF_MEMORY)
			return -ENOMEM;
		return -ENODEV;
	}

	hpet = table.ptr;
	if (hpet->address.address_space_id != ACPI_AS_ID_SYS_MEM)
		return -EIO;
	physaddr_t address = hpet->address.address;
	hpet_virtual = iomap(address, PAGE_SIZE, MMU_READ | MMU_WRITE);
	if (!hpet_virtual)
		return -ENOMEM;

	/* After this point, we need cleanup on error */
	int err;

	/* Get the capabilities and the clock period, and use the clock period to get the frequency */
	u64 caps = hpet_read(HPET_REG_CAPS);
	u32 femtoperiod = caps >> 32;
	if (unlikely(femtoperiod == 0)) {
		err = -EIO;
		goto err_cleanup;
	}
	_hpet_source.freq = 1000000000000000ull / femtoperiod;
	_hpet_source.get_ticks = get_ticks;
	_hpet_source.private = NULL;

	/* Make sure the HPET is disabled and set the initial count to zero */
	hpet_write(HPET_REG_CONFIG, 0);
	hpet_write(HPET_REG_COUNTER, 0);

	/* Make sure the HPET supports 64 bit */
	if (!(hpet->block_id & ACPI_HPET_COUNT_SIZE_CAP)) {
		err = -ENOSYS;
		goto err_cleanup;
	}

	/* May be accessed by another CPU, so do a memory fence here */
	hpet_source = &_hpet_source;
	atomic_thread_fence(ATOMIC_SEQ_CST);
	*out = hpet_source;

	/* Enable the HPET */
	hpet_write(HPET_REG_CONFIG, 1);
	return 0;
err_cleanup:
	if (unlikely(iounmap(hpet_virtual, PAGE_SIZE)))
		printk(PRINTK_ERR "core: Failed to unmap IO memory for HPET in error handling\n");
	hpet_virtual = NULL;

	return err;
}

static struct timekeeper __timekeeper hpet_timekeeper = {
	.name = "hpet",
	.init = init,
	.rating = 60, /* Slow to access, but very accurate */
	.early = true
};
