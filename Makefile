-include .config

ASMFLAGS = -c -MMD -MP -I./include \
	   -include ./include/generated/autoconf.h
CFLAGS = -c -MMD -MP -std=c11 -I./include \
	 -include ./include/generated/autoconf.h \
	 -ffreestanding -fno-stack-protector -fno-omit-frame-pointer -fno-optimize-sibling-calls \
	 -Wall -Wextra -Wshadow -Wpointer-arith -Winline \
	 -Wimplicit-fallthrough -Wvla -Walloca -Wstrict-prototypes \
	 -Wmissing-prototypes -Wno-attributes \
	 -mno-red-zone -mgeneral-regs-only \
	 -O$(CONFIG_OPTIMIZATION)
SPARSE_CFLAGS = -D__BIGGEST_ALIGNMENT__=16 # shut up sparse about __BIGGEST_ALIGNMENT__ not being defined
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

OUTPUT := ./lunar
ifeq ($(CONFIG_KASLR), y)
LDSCRIPT := ./kernel/linker_x86_64_kaslr.ld
else
LDSCRIPT := ./kernel/linker_x86_64.ld
endif

LIBGCC_DIR = $(shell dirname $(shell $(CC) $(CFLAGS) -print-libgcc-file-name))
LDFLAGS += -L$(LIBGCC_DIR)

.PHONY: all menuconfig clean

all: $(OUTPUT)

BUILD_MAKEFILES = $(shell find ./kernel ./drivers ./firmware ./fs -type f -name 'Makefile')
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
	@sparse $(CFLAGS) $(SPARSE_CFLAGS) $<
	@$(CC) $(CFLAGS) $< -o $@

clean:
	@rm -f $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(H_DEPENDENCIES) $(OUTPUT)
	@echo "[CLEAN] ."
