#include <crescent/core/apic.h>
#include <crescent/core/printk.h>
#include <crescent/mm/heap.h>
#include <crescent/mm/vmm.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <acpi/madt.h>

static struct ioapic_desc* ioapics = NULL;
unsigned long ioapic_count;

static struct acpi_madt* madt;

static unsigned long get_entry_count(u8 type) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);

	unsigned long count = 0;
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type)
			count++;
		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return count;
}

static void* get_entry(u8 type, unsigned long n) {
	struct acpi_entry_hdr* entry = (struct acpi_entry_hdr*)(madt + 1);
	while (entry < (struct acpi_entry_hdr*)((u8*)madt + madt->hdr.length)) {
		if (entry->type == type) {
			if (n-- == 0)
				return entry;
		}

		entry = (struct acpi_entry_hdr*)((u8*)entry + entry->length);
	}

	return NULL;
}

static struct ioapic_desc* get_ioapic_gsi(u32 gsi) {
	for (unsigned long i = 0; i < ioapic_count; i++) {
		if (ioapics[i].base <= gsi && ioapics[i].top > gsi)
			return &ioapics[i];
	}

	return NULL;
}

static struct ioapic_desc* get_ioapics(void) {
	return ioapics;
}

const struct acpi_madt_ops ops = {
	.get_entry_count = get_entry_count,
	.get_entry = get_entry,
	.get_ioapic_gsi = get_ioapic_gsi,
	.get_ioapics = get_ioapics
};

uacpi_status acpi_madt_init(void) {
	uacpi_table table;
	int err = uacpi_table_find_by_signature("APIC", &table);
	if (err != UACPI_STATUS_OK)
		return err;

	madt = table.ptr;

	ioapic_count = get_entry_count(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	ioapics = kmalloc(sizeof(*ioapics) * ioapic_count, MM_ZONE_NORMAL);
	if (!ioapics) {
		err = UACPI_STATUS_OUT_OF_MEMORY;
		goto err;
	}

	for (unsigned long i = 0; i < ioapic_count; i++) {
		struct acpi_madt_ioapic* entry = get_entry(ACPI_MADT_ENTRY_TYPE_IOAPIC, i);
		if (unlikely(!entry)) {
			err = UACPI_STATUS_INTERNAL_ERROR;
			goto err;
		}
		if (unlikely(entry->address % PAGE_SIZE != 0)) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}

		ioapics[i].address = iomap(entry->address, PAGE_SIZE, MMU_READ | MMU_WRITE);
		if (!ioapics[i].address) {
			err = UACPI_STATUS_MAPPING_FAILED;
			goto err;
		}

		ioapics[i].base = entry->gsi_base;
		u32 _top = (ioapic_read(ioapics[i].address, IOAPIC_REG_ENTRY_COUNT) >> 16 & 0xFF) + 1;
		ioapics[i].top = ioapics[i].base + _top;
		printk(PRINTK_DBG "acpi: ioapic%lu at base: %u, top: %u\n", i, ioapics[i].base, ioapics[i].top);
	}

	apic_set_madt_ops(&ops);
	return UACPI_STATUS_OK;
err:
	if (ioapics) {
		for (unsigned long i = 0; i < ioapic_count; i++) {
			if (ioapics[i].address)
				iounmap(ioapics[i].address, PAGE_SIZE);
		}
		kfree(ioapics);
	}
	madt = NULL;
	return err;
}
