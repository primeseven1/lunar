C_SOURCE_FILES += $(shell find ./drivers/acpi/uacpi/source -type f -name '*.c')
C_SOURCE_FILES += ./drivers/acpi/main.c ./drivers/acpi/glue.c ./drivers/acpi/madt.c

UACPI_CFLAGS := -DUACPI_BAREBONES_MODE -DUACPI_OVERRIDE_TYPES \
		-I./drivers/acpi/include -I./drivers/acpi/uacpi/include

./drivers/acpi/%.o: ./drivers/acpi/%.c
	$(CC) $(CFLAGS) $(UACPI_CFLAGS) $< -o $@
