ARCH ?= x86_64
TOOLCHAIN ?= gcc
BUILD_HOST_OS := $(shell uname -s)
BUILD := build/$(ARCH)/$(TOOLCHAIN)

ifeq ($(filter $(ARCH),x86_64 arm64),)
$(error Unsupported ARCH '$(ARCH)'; choose x86_64 or arm64)
endif

# Override KCC/KLD to use a toolchain installed in a nonstandard location.
ifeq ($(TOOLCHAIN),llvm)
KCC ?= clang
KLD ?= ld.lld
TARGET_x86_64 := x86_64-none-elf
TARGET_arm64 := aarch64-none-elf
TARGET_FLAGS := --target=$(TARGET_$(ARCH))
else ifeq ($(TOOLCHAIN),gcc)
ifeq ($(BUILD_HOST_OS),Darwin)
# Homebrew ELF tools; Apple's native compiler/linker normally target macOS.
PREFIX_x86_64 := x86_64-elf-
PREFIX_arm64 := aarch64-elf-
else
PREFIX_x86_64 := x86_64-linux-gnu-
PREFIX_arm64 := aarch64-linux-gnu-
endif
KCC ?= $(PREFIX_$(ARCH))gcc
KLD ?= $(PREFIX_$(ARCH))ld
else
$(error Unsupported TOOLCHAIN '$(TOOLCHAIN)'; choose gcc or llvm)
endif

COMMON_FLAGS := $(TARGET_FLAGS) -ffreestanding -fno-stack-protector -fno-pie \
                -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables \
                -g -Iinclude
CFLAGS := $(COMMON_FLAGS) -std=c11 -Os -ffunction-sections -fdata-sections \
          -Wall -Wextra -Werror -MMD -MP
ifeq ($(ARCH),x86_64)
CFLAGS += -m64 -mno-red-zone -mgeneral-regs-only
IMAGE := $(BUILD)/aios.img
QEMU := qemu-system-x86_64
QEMU_ARGS := -machine pc -m 128M -smp 1 -drive format=raw,file=$(IMAGE),if=ide
else
CFLAGS += -mgeneral-regs-only
IMAGE := $(BUILD)/kernel.elf
QEMU := qemu-system-aarch64
QEMU_ARGS := -machine virt,secure=off,virtualization=off -cpu cortex-a53 -m 128M \
             -smp 1 -kernel $(IMAGE)
endif
QEMU_ARGS += -nographic -no-reboot

OBJECTS := $(BUILD)/start.o $(BUILD)/main.o $(BUILD)/platform.o
OBJECTS += $(BUILD)/periodic.o $(BUILD)/sensor.o
OBJECTS += $(BUILD)/distance_queue.o $(BUILD)/controller.o
OBJECTS += $(BUILD)/telemetry.o
ifeq ($(ARCH),x86_64)
OBJECTS += $(BUILD)/interrupts.o $(BUILD)/interrupt_entry.o
endif

.PHONY: all run debug observe observe-debug check test smoke size clean check-tools help
all: $(IMAGE)

help:
	@echo 'make [ARCH=x86_64|arm64] [TOOLCHAIN=gcc|llvm]'
	@echo 'Targets: all, check, test, run, smoke, size, debug, observe, observe-debug, clean'

check-tools:
	@command -v $(KCC) >/dev/null || { echo 'Missing compiler: $(KCC). See README.md.'; exit 1; }
	@command -v $(KLD) >/dev/null || { echo 'Missing linker: $(KLD). See README.md.'; exit 1; }

$(BUILD):
	mkdir -p $@

$(BUILD)/start.o: arch/$(ARCH)/start.S | $(BUILD) check-tools
	$(KCC) $(COMMON_FLAGS) -c $< -o $@

$(BUILD)/main.o: kernel/main.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/periodic.o: kernel/periodic.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/sensor.o: kernel/sensor.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/distance_queue.o: kernel/distance_queue.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/controller.o: kernel/controller.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/telemetry.o: kernel/telemetry.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/platform.o: arch/$(ARCH)/platform.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: arch/x86_64/interrupts.c | $(BUILD) check-tools
	$(KCC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupt_entry.o: arch/x86_64/interrupt_entry.S | $(BUILD) check-tools
	$(KCC) $(COMMON_FLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJECTS) arch/$(ARCH)/linker.ld
	$(KLD) --build-id=none --gc-sections -T arch/$(ARCH)/linker.ld -Map=$(BUILD)/kernel.map -o $@ $(OBJECTS)

$(BUILD)/boot.o: arch/x86_64/boot.S | $(BUILD) check-tools
	$(KCC) $(COMMON_FLAGS) -c $< -o $@

$(BUILD)/boot.bin: $(BUILD)/boot.o
	$(KLD) --build-id=none -Ttext 0x7c00 --oformat=binary -o $@ $<

$(BUILD)/aios.img: $(BUILD)/boot.bin $(BUILD)/kernel.elf scripts/image.py
	python3 scripts/image.py $< $(BUILD)/kernel.elf $@

run: all
	$(QEMU) $(QEMU_ARGS)

debug: all
	$(QEMU) $(QEMU_ARGS) -S -s

observe-debug: OBSERVE_FLAGS = -S -s
observe observe-debug: all
ifeq ($(ARCH),x86_64)
	@echo 'Telemetry: $(BUILD)/telemetry.bin (new recording on each launch)'
	$(QEMU) $(QEMU_ARGS) -monitor none -serial mon:stdio -serial file:$(BUILD)/telemetry.bin $(OBSERVE_FLAGS)
else
	@echo 'observe currently requires ARCH=x86_64 (COM2 telemetry).'; exit 1
endif

check: all
	python3 scripts/check.py $(ARCH) $(BUILD)/kernel.elf $(IMAGE)

test:
	mkdir -p build/tests
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/main.c kernel/periodic.c kernel/sensor.c kernel/distance_queue.c kernel/controller.c kernel/telemetry.c tests/console.c -o build/tests/console
	build/tests/console
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/periodic.c kernel/sensor.c kernel/distance_queue.c tests/periodic.c -o build/tests/periodic
	build/tests/periodic
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/periodic.c kernel/sensor.c kernel/distance_queue.c kernel/controller.c tests/messaging.c -o build/tests/messaging
	build/tests/messaging
	$(CC) -std=c11 -Wall -Wextra -Werror -Iinclude kernel/periodic.c kernel/sensor.c kernel/distance_queue.c kernel/controller.c kernel/telemetry.c tests/telemetry.c -o build/tests/telemetry
	build/tests/telemetry build/tests/telemetry.bin
	python3 tests/telemetry_test.py

smoke: check
	python3 scripts/smoke.py $(ARCH) $(QEMU) $(QEMU_ARGS)

size: all
	python3 scripts/size.py $(BUILD)/kernel.elf $(IMAGE)

clean:
	rm -rf build

-include $(BUILD)/*.d
