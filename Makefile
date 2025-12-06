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

USER_INITCODE = user/initcode
INITCODE_H = include/proc/initcode.h

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

all: userinit $(TARGET_ELF)

# 用户程序编译目标
userinit: $(INITCODE_H)

$(INITCODE_H): $(USER_INITCODE).c
	@echo "user code compiling..."
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -Os -c $(USER_INITCODE).c -o $(USER_INITCODE).o
	$(LD) -N -e main -Ttext 0 -o $(USER_INITCODE).out $(USER_INITCODE).o
	$(OBJCOPY) -S -O binary $(USER_INITCODE).out $(USER_INITCODE)
	xxd -i $(USER_INITCODE) > ./include/proc/initcode.h
	rm -f $(USER_INITCODE) $(USER_INITCODE).o $(USER_INITCODE).out $(USER_INITCODE).d

$(TARGET_ELF): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(TARGET_ELF) $(shell find kernel -name '*.o' -o -name '*.d')

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
