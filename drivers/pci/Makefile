ifeq ($(CONFIG_DRIVER_PCI), y)

C_SOURCE_FILES += $(shell find ./drivers/pci -type f -name '*.c')

./drivers/pci/%.o: ./drivers/pci/%.c
	@echo "[CC] $<"
	@sparse $(CFLAGS) $<
	@$(CC) $(CFLAGS) $< -o $@

endif
