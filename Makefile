TOOLCHAIN = riscv64-unknown-elf-

GDB = gdb-multiarch

CC = $(TOOLCHAIN)gcc
LD = $(TOOLCHAIN)ld
OBJCOPY = $(TOOLCHAIN)objcopy
OBJDUMP = $(TOOLCHAIN)objdump

CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb -MD
CFLAGS += -ffreestanding -nostdlib -mno-relax -mcmodel=medany
CFLAGS += -Iinclude

LDFLAGS = -T scripts/kernel.ld -nostdlib

# 查找所有文件，其中entry.S被单独处理
ENTRY_S = kernel/boot/entry.S
SOURCES_S_OTHER = $(filter-out $(ENTRY_S), $(shell find kernel -name '*.S'))
SOURCES_C = $(shell find kernel -name '*.c')

# 转换 .o 文件
OBJECT_ENTRY = $(patsubst %.S, %.o, $(ENTRY_S))
OBJECTS_S_OTHER = $(patsubst %.S, %.o, $(SOURCES_S_OTHER))
OBJECTS_C = $(patsubst %.c, %.o, $(SOURCES_C))

# 确保 OBJECT_ENTRY (entry.o) 在链接顺序的最前面
OBJECTS = $(OBJECT_ENTRY) $(OBJECTS_S_OTHER) $(OBJECTS_C)
DEPS = $(patsubst %.o, %.d, $(OBJECTS))
TARGET_ELF = kernel.elf

QEMU_OPTS = -machine virt -bios none -kernel $(TARGET_ELF) -nographic

.PHONY: all clean qemu qemu-gdb

all: $(TARGET_ELF)

$(TARGET_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf kernel.elf $(shell find kernel -name '*.o' -o -name '*.d')

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
