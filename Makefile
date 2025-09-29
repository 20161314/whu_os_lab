TOOLCHAIN = riscv64-unknown-elf-

GDB = gdb-multiarch

CC = $(TOOLCHAIN)gcc
LD = $(TOOLCHAIN)ld
OBJCOPY = $(TOOLCHAIN)objcopy
OBJDUMP = $(TOOLCHAIN)objdump

CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb -MD
CFLAGS += -ffreestanding -nostdlib -mno-relax -mcmodel=medany
CFLAGS += -Iinclude

LDFLAGS = -T kernel/kernel.ld -nostdlib

SOURCES_S = $(wildcard kernel/*.S)
SOURCES_C = $(wildcard kernel/*.c)
OBJECTS_S = $(patsubst %.S, %.o, $(SOURCES_S))
OBJECTS_C = $(patsubst %.c, %.o, $(SOURCES_C))

OBJECTS = $(OBJECTS_S) $(OBJECTS_C)
DEPS = $(patsubst %.o, %.d, $(OBJECTS))
TARGET_ELF = kernel/kernel.elf
# The fix is here! Removed "-serial stdio"
QEMU_OPTS = -machine virt -bios none -kernel $(TARGET_ELF) -nographic

.PHONY: all clean qemu qemu-gdb debug

all: $(TARGET_ELF)

$(TARGET_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f kernel/*.o kernel/*.d $(TARGET_ELF)

qemu: $(TARGET_ELF)
	@echo "Starting QEMU..."
	@qemu-system-riscv64 $(QEMU_OPTS)

qemu-gdb: $(TARGET_ELF)
	@echo "Starting QEMU for GDB debugging..."
	@qemu-system-riscv64 $(QEMU_OPTS) -S -s

debug: $(TARGET_ELF)
	@tmux kill-session -t kernel_debug 2>/dev/null || true
	@tmux new-session -d -s kernel_debug "make qemu-gdb" \; \
		split-window -h "sleep 1; $(GDB) -ex 'target remote localhost:1234' $(TARGET_ELF)" \; \
		attach-session -t kernel_debug

-include $(DEPS)
