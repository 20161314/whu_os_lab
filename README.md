
# My RISC-V OS Project

## RISC-V OS - 实验一 & 实验二

这是一个从零开始构建的、逐步完善的 RISC-V 操作系统内核。本项目目前已完成：
1.  **实验一：最小化内核启动** - 搭建了一个能在 QEMU 模拟器上成功引导，并向串口输出信息的最小化系统。
2.  **实验二：内核 `printf` 与清屏** - 在实验一的基础上，实现了功能丰富的内核格式化输出函数 `printf` 和清屏功能，为后续开发提供了强大的调试工具。

## 功能实现

本项目实现了一个操作系统最核心的启动流程和基础工具：

-   **汇编入口 (`entry.S`)**: 设置初始的栈指针，为 C 语言环境做准备，并清零 `.bss` 段。
-   **链接脚本 (`kernel.ld`)**: 定义了内核代码在内存中的布局，设定了 `0x80000000` 作为内核的起始地址，与 xv6 保持一致。
-   **串口 (UART) 驱动 (`uart.c`)**: 一个简单的轮询式 UART 驱动，作为所有底层字符输出的基础。
-   **格式化输出 (`printf.c`)**: 实现了一个支持 `%d`, `%x`, `%s`, `%c`, `%%` 等常用格式的 `printf` 函数，提供更丰富的内核调试信息输出能力。
-   **清屏功能**: 通过发送 ANSI 转义序列实现了清屏功能，便于刷新终端显示。
-   **C 主函数 (`main.c`)**: 在汇编代码完成初始化后，跳转到此 C 函数。它调用新增的函数来测试内核功能，然后使系统进入休眠状态。
-   **模块化代码结构**: 项目包含 `include` 目录用于存放头文件，使接口与实现分离，代码结构更清晰、更规范。

## 环境要求

在开始之前，请确保您已经安装了 RISC-V 交叉编译工具链和 QEMU 模拟器。

-   **交叉编译工具链**: `riscv64-unknown-elf-gcc`, `riscv64-unknown-elf-ld`, 等。
-   **模拟器**: `qemu-system-riscv64`

## 如何构建和运行

本项目使用 `Makefile` 进行自动化管理。

#### 编译内核

在项目根目录下，执行以下命令来编译所有源代码并链接生成内核文件 `kernel.elf`。

```bash
make
````

或者

```bash
make all
```

#### 在 QEMU 中运行

编译成功后，执行以下命令启动 QEMU 并加载内核：

```bash
make qemu
```

#### 清理生成文件

若要清除所有编译生成的目标文件 (`.o`, `.d`) 和内核镜像，请执行：

```bash
make clean
```

#### 使用 GDB 调试

若要启动 QEMU 并等待 GDB 连接（监听在 1234 端口），请执行：

```bash
make qemu-gdb
```

本项目还提供了一个集成的调试命令，它会自动启动 `tmux` 并为您安排好 QEMU 和 GDB 的窗口：

```bash
make debug
```

## 预期输出

当您成功运行 `make qemu` 后，您应该会在终端看到 QEMU 启动，并打印出类似以下的信息（具体内容取决于你在 `main.c` 中的测试代码）：

```
Screen cleared!
====== printf test start ======
Testing integer: 42
Testing negative: -123
Testing zero: 0
Testing hex: 0xabc
Testing string: Hello RISC-V OS!
Testing char: A
Testing percent: %
====== printf test end ======
```

*(随后屏幕会被再次清除)*

## 文件结构说明

```
.
├── include/
│   └── console.h      # 项目共享的头文件，定义函数原型
├── kernel/
│   ├── entry.S        # 汇编入口与早期初始化
│   ├── kernel.ld      # 链接器脚本，定义内存布局
│   ├── main.c         # C代码主函数
│   ├── printf.c       # printf 和清屏功能的实现
│   └── uart.c         # 串口驱动程序
├── .vscode/
│   └── c_cpp_properties.json # VS Code 编辑器配置文件 (可选)
├── Makefile         # 自动化构建脚本
└── README.md        # 本说明文件
```