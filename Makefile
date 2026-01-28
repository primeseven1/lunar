-include .config

ASMFLAGS = -c -MMD -MP -I./include \
	   -include ./include/generated/autoconf.h
CFLAGS = -c -MMD -MP -std=c11 -I./include \
	 -include ./include/generated/autoconf.h \
	 -ffreestanding -fno-stack-protector -fno-omit-frame-pointer \
	 -fno-optimize-sibling-calls -fno-exceptions -fno-asynchronous-unwind-tables \
	 -Wall -Wextra -Wshadow -Wpointer-arith -Winline \
	 -Wimplicit-fallthrough -Wvla -Walloca -Wstrict-prototypes \
	 -Wmissing-prototypes -Wno-attributes \
	 -mno-red-zone -mgeneral-regs-only \
	 -O$(CONFIG_OPTIMIZATION)
# Primarily used to prevent sparse from spitting out a bunch of warnings we don't care about
SPARSE_CFLAGS = -D__BIGGEST_ALIGNMENT__=16 \
		-D__clang_major__ -D__clang_minor__ -D__clang_patchlevel__
LDFLAGS = -static -nostdlib --no-dynamic-linker \
	  -ztext -zmax-page-size=0x1000 \
	  -O$(CONFIG_OPTIMIZATION)

ifeq ($(CONFIG_LLVM), y)
CC := clang
LD := ld.lld
CFLAGS += -fcolor-diagnostics --target=x86_64-unknown-elf
CC_MIN_MAJOR := 14
CC_MIN_MINOR := 0
LD_MIN_MAJOR := 14
LD_MIN_MINOR := 0
RTLIB_DIR := tools/rt/
RTLIB := clang_rt.builtins-x86_64
else
CC := x86_64-elf-gcc
LD := x86_64-elf-ld
CC_MIN_MAJOR := 12
CC_MIN_MINOR := 2
LD_MIN_MAJOR := 2
LD_MIN_MINOR := 39
RTLIB_DIR := $(shell dirname $(shell $(CC) $(CFLAGS) -print-libgcc-file-name))
RTLIB := gcc
endif

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

LDFLAGS += -L$(RTLIB_DIR)

.PHONY: all version menuconfig clean

all: version $(OUTPUT)

version:
	@scripts/cc-ver.sh $(CC) $(CC_MIN_MAJOR) $(CC_MIN_MINOR)
	@scripts/ld-ver.sh $(LD) $(LD_MIN_MAJOR) $(LD_MIN_MINOR)
	@if [ "$(CONFIG_LLVM)" = "y" ]; then scripts/clangrt.sh; fi

BUILD_MAKEFILES = $(shell find ./kernel ./drivers ./firmware ./fs -type f -name 'Makefile')
-include $(BUILD_MAKEFILES)

S_OBJECT_FILES := $(patsubst %.S, %.o, $(S_SOURCE_FILES))
C_OBJECT_FILES := $(patsubst %.c, %.o, $(C_SOURCE_FILES))
H_DEPENDENCIES := $(patsubst %.o, %.d, $(S_OBJECT_FILES) $(C_OBJECT_FILES))
-include $(H_DEPENDENCIES)

menuconfig:
	@scripts/mconf.sh

$(OUTPUT): $(S_OBJECT_FILES) $(C_OBJECT_FILES) $(LDSCRIPT)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) $(S_OBJECT_FILES) $(C_OBJECT_FILES) -T$(LDSCRIPT) -o $(OUTPUT) -l$(RTLIB)
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
