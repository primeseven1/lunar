C_SOURCE_FILES += $(shell find ./firmware/acpi/uacpi/source -type f -name '*.c')
C_SOURCE_FILES += ./firmware/acpi/init.c ./firmware/acpi/glue.c ./firmware/acpi/events.c

CFLAGS += -DUACPI_NATIVE_ALLOC_ZEROED -DUACPI_KERNEL_INITIALIZATION \
	  -DUACPI_OVERRIDE_TYPES -I./firmware/acpi/uacpi/include -I./include/acpi
