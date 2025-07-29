-include .config

ASMFLAGS = -c -MMD -MP -I./include \
	   -include ./include/generated/autoconf.h
CFLAGS = -c -MMD -MP -std=c11 -I./include \
	 -include ./include/generated/autoconf.h \
	 -ffreestanding -fno-stack-protector \
	 -fno-omit-frame-pointer -fwrapv \
	 -Wall -Wextra -Wshadow -Wpointer-arith \
	 -Winline -Wimplicit-fallthrough -Wvla -Walloca \
	 -Wstrict-prototypes -Wmissing-prototypes -Wno-attributes \
	 -mno-red-zone -mgeneral-regs-only \
	 -O$(CONFIG_OPTIMIZATION)
LDFLAGS = -static -nostdlib --no-dynamic-linker \
	  -ztext -zmax-page-size=0x1000 \
	  -O$(CONFIG_OPTIMIZATION)

CC := x86_64-elf-gcc
LD := x86_64-elf-ld

ifeq ($(CONFIG_DEBUG), y)
ASMFLAGS += -g
CFLAGS += -g
endif

ifeq ($(CONFIG_KASLR), y)
CFLAGS += -fPIE
LDFLAGS += -pie
else
CFLAGS += -mcmodel=kernel
LDFLAGS += -no-pie
endif

ifeq ($(CONFIG_UBSAN), y)
CFLAGS += -fsanitize=undefined
endif

OUTPUT := ./crescent
ifeq ($(CONFIG_KASLR), y)
LDSCRIPT := ./kernel/linker_x86_64_kaslr.ld
else
LDSCRIPT := ./kernel/linker_x86_64.ld
endif

LIBGCC_DIR = $(shell dirname $(shell $(CC) $(CFLAGS) -print-libgcc-file-name))
LDFLAGS += -L$(LIBGCC_DIR)

.PHONY: all menuconfig clean iso

all: $(OUTPUT)

BUILD_MAKEFILES = $(shell find ./kernel ./drivers ./firmware -type f -name 'Makefile')
-include $(BUILD_MAKEFILES)

S_OBJECT_FILES := $(patsubst %.S, %.o, $(S_SOURCE_FILES))
C_OBJECT_FILES := $(patsubst %.c, %.o, $(C_SOURCE_FILES))
H_DEPENDENCIES := $(patsubst %.o, %.d, $(S_OBJECT_FILES) $(C_OBJECT_FILES))
-include $(H_DEPENDENCIES)

menuconfig:
	scripts/mconf.sh

$(OUTPUT): $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(LDSCRIPT)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) $(S_OBJECT_FILES) $(C_OBJECT_FILES) -T$(LDSCRIPT) -o $(OUTPUT) -lgcc
	@echo "[BUILD] Kernel image $@ is ready!"

%.o: %.S
	@echo "[AS] $<"
	@$(CC) $(ASMFLAGS) $< -o $@

%.o: %.c
	@echo "[CC] $<"
	@sparse $(CFLAGS) $<
	@$(CC) $(CFLAGS) $< -o $@

clean:
	@rm -f $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(H_DEPENDENCIES) $(OUTPUT)
	@echo "[CLEAN] ."

ISO_ROOT := tools/testing/iso
ISO_OUTPUT := tools/testing/crescent.iso

iso: $(OUTPUT)
	@cp $(OUTPUT) $(ISO_ROOT)
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin \
		--efi-boot-part --efi-boot-image --protective-msdos-label $(ISO_ROOT) -o $(ISO_OUTPUT) &> /dev/null
	@./tools/limine/limine bios-install $(ISO_OUTPUT) &> /dev/null
	@echo "[ISO] $(ISO_OUTPUT)"
