# 实验4

## 代码
```c
#include "riscv.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"

void main()
{
    // clear_screen();

    uart_init();          // 1. 基础依赖：初始化串口，用于后续打印信息。
    pmem_init();          // 2. 基础依赖：初始化物理内存管理。
    kvm_init();           // 3. 基础依赖：创建内核页表。
    kvm_inithart();       // 4. 基础依赖：为当前核心启用分页机制。
    
    // --- 以下是实验4的核心实现 ---

    trap_kernel_init();   // 5. 设置S模式中断向量 (stvec)
    trap_kernel_inithart(); // 6. 使能S模式下的各类中断 (sie)
    plic_init();          // 7. 初始化PLIC，用于处理外部中断
    plic_inithart();      // 8. 为当前核心配置PLIC

    printf("Hello OS\n"); // 打印信息，表示初始化完成

    intr_on();            // 9. 打开全局中断总开关 (sstatus.SIE)

    while(1);             // 10. 进入空闲等待循环，等待中断发生
}

```


### 第一个文件：`main.c`  - 内核主函数

这个文件是您操作系统的C语言入口，负责按正确的顺序“点亮”和初始化所有核心子系统。

```c
#include "riscv.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
```
*   **作用与含义**：这是一系列的头文件包含。它们的作用是引入其他模块的函数声明、宏定义和类型定义，以便`main`函数可以使用它们。
    *   `riscv.h`: 包含RISC-V架构相关的底层宏和函数，如读写CSR寄存器（`r_satp`, `w_stvec`等）、中断控制（`intr_on`）等。
    *   `dev/console.h`: 包含控制台（如UART串口）的接口，主要是`printf`和`uart_init`。
    *   `dev/timer.h`: 包含时钟相关的接口。
    *   `dev/plic.h`: 包含平台级中断控制器（PLIC）的接口。
    *   `lib/str.h`: 包含字符串处理函数（虽然在此文件中未使用）。
    *   `proc/proc.h`: 包含进程管理相关的接口（为未来做准备）。
    *   `mem/pmem.h`: 包含物理内存管理（PMM）的接口。
    *   `mem/vmem.h`: 包含虚拟内存管理（VMM）的接口。
    *   `trap/trap.h`: 包含中断和异常（Trap）处理的接口。


void main()
{

*   **作用与含义**：这是内核的C语言主函数。在汇编启动代码（如`entry.S`）完成最底层的设置（如设置栈指针）后，会跳转到这里开始执行。

```c
    // clear_screen();
```
*   **作用与含义**：这是一行被注释掉的代码。如果取消注释，它的作用是调用一个函数来清空屏幕，通常是通过发送ANSI转义序列实现的。

```c
    uart_init();
```
*   **作用与含义**：**初始化UART（通用异步收发传输器），即串口设备**。这是**至关重要**的第一步，因为它使得`printf`函数能够工作。没有它，内核将是一个无法输出任何信息的“黑盒子”，调试将变得极其困难。

```c
    pmem_init();
```
*   **作用与含义**：**初始化物理内存管理器（PMM）**。这个函数会扫描并建立一个空闲物理页面的数据结构（通常是一个链表），为后续的内存分配做好准备。这是所有需要动态内存分配功能（如创建页表）的基础。

```c
    kvm_init();
```
*   **作用与含义**：**初始化内核虚拟内存（Kernel Virtual Memory），即创建内核页表**。此函数会分配页表页，并建立内核代码、数据、以及外设IO内存区域的映射关系（通常是恒等映射）。此时，页表已在内存中构建完成，但尚未被CPU使用。

```c
    kvm_inithart();
```
*   **作用与含义**：**为当前核心（Hart）激活虚拟内存**。这个函数会将`kvm_init`创建好的内核页表的物理地址写入`satp`寄存器，并执行`sfence.vma`指令刷新TLB。**从这条指令执行完毕的下一刻起，CPU就从物理寻址模式切换到了虚拟寻址模式**。这个顺序（先`init`再`inithart`）是绝对不能错的。

```c
    trap_kernel_init();
```
*   **作用与含义**：**执行全局的、一次性的Trap初始化**。它负责设置那些所有CPU核心共享的、与中断/异常相关的机制。从您的代码看，它调用了`timer_create()`，可能是用来设置CLINT（核心本地中断器）时钟的。

```c
    trap_kernel_inithart();
```
*   **作用与含义**：**为当前核心（Hart）初始化Trap机制**。这个函数会将S模式的陷阱处理程序的入口地址（`kernel_vector`）写入`stvec`（Supervisor Trap Vector）寄存器。这样，当发生中断或异常时，CPU就知道应该跳转到哪里去执行处理代码。

```c
    plic_init();
```
*   **作用与含义**：**执行全局的、一次性的PLIC（平台级中断控制器）初始化**。PLIC负责管理来自外部设备（如磁盘、网卡、键盘）的中断。此函数通常会设置各个中断源的优先级。

```c
    plic_inithart();
```
*   **作用与含义**：**为当前核心（Hart）初始化PLIC**。此函数会为当前CPU核心使能它所关心的外部中断（例如，使能UART中断），并设置该核心的中断优先级阈值。

```c
    printf("Hello OS\n");
```
*   **作用与含义**：打印“Hello OS”。这不仅仅是一句问候，更是一个**重要的里程碑**。如果能看到这行输出，证明上述所有核心子系统——串口、物理内存、虚拟内存（创建和激活）、中断向量设置——都已成功初始化。

```c
    intr_on();
```
*   **作用与含义**：**开启中断**。该函数会设置`sstatus`寄存器中的`SIE`（Supervisor Interrupt Enable）位，允许CPU响应S模式的中断。在此之前，所有中断都是被屏蔽的。从此，系统才能响应时钟中断、外部设备中断等。

```c
    while(1);
}
```
*   **作用与含义**：**内核空闲循环（Idle Loop）**。`main`函数永远不会返回。CPU会在这里无限循环。当没有任务可执行时，系统就在这里“空转”。当一个中断发生时，CPU会跳转到中断处理程序，处理完毕后，再返回到这个循环中继续等待下一个中断。这是操作系统事件驱动模型的基础。

---

### 第二个文件：`trap.c` (假设) - 中断与异常处理

这个文件是操作系统的“神经中枢”，负责响应所有来自硬件的意外事件（中断和异常）。

```c
// 中断信息
static char* interrupt_info[16] = { ... };
// 异常信息
static char* exception_info[16] = { ... };
```
*   **作用与含义**：这两个静态字符串数组是**调试辅助工具**。它们将RISC-V规范中定义的16种中断和16种异常的数字编码映射为人类可读的字符串。当发生未知的中断或异常时，可以方便地打印出其名称，极大地帮助了调试。

```c
// in trap.S
// 内核中断处理流程
extern void kernel_vector();
```
*   **作用与含义**：这是一个**外部函数声明**。它告诉C编译器，有一个名为`kernel_vector`的函数存在，但它的定义在别处（`trap.S`汇编文件中）。`kernel_vector`是S模式中断/异常处理的汇编入口点，负责保存寄存器现场并调用C语言处理函数。

```c
void trap_kernel_init()
{
    timer_create();
}
```
*   **作用与含义**：如`main`函数中分析，这是全局Trap初始化。`timer_create()`可能是对CLINT（核心本地中断器）进行设置，比如确定时钟频率，为后续设置时钟中断做准备。

```c
void trap_kernel_inithart()
{
    w_stvec((uint64)kernel_vector);
}
```
*   **作用与含义**：这是每个核心的Trap初始化。`w_stvec`是一个宏，它将`kernel_vector`函数的地址写入`stvec`（Supervisor Trap Vector）寄存器。**这是将软件处理程序与硬件事件关联起来的关键一步**。

```c
void external_interrupt_handler()
{
    int irq = plic_claim();
```
*   **作用与含义**：当发生外部中断时，首先调用`plic_claim()`向PLIC“认领”中断。这个函数会返回一个整数`irq`，代表是哪个设备触发了中断（例如，10代表UART）。这个操作是原子的，并且会告诉PLIC“这个中断我正在处理了”。

```c
    switch(irq){
        // ...
        case UART_IRQ:
            uart_intr();
            break;
        // ...
    }
```
*   **作用与含义**：这是一个**中断分发器**。根据`plic_claim()`返回的`irq`号，`switch`语句将中断分发给对应的设备驱动中断处理函数。例如，如果是UART中断，就调用`uart_intr()`。

```c
    if(irq)
        plic_complete(irq);
}
```
*   **作用与含义**：在处理完中断后，调用`plic_complete(irq)`通知PLIC“我已经处理完这个中断了”。这使得PLIC可以继续为该设备传递新的中断信号。**`claim`和`complete`必须成对出现**。

```c
void timer_interrupt_handler()
{
    // ...
    if(mycpuid() == 0){
        timer_update();
    }
```
*   **作用与含义**：这是时钟中断处理函数。`if(mycpuid() == 0)`确保只有一个核心（通常是0号核心）来执行`timer_update()`，这通常是更新一个全局的“滴答”（ticks）计数器，以避免多核竞争。`timer_update()`还会重新设置下一次时钟中断的时间。

```c
    w_sip(r_sip() & ~2);
}
```
*   **作用与含义**：**确认S模式软件中断**。在RISC-V中，时钟中断通常由M模式固件（如OpenSBI）捕获，然后通过触发一个S模式的软件中断（Supervisor Software Interrupt Pending, SSIP）来通知S模式内核。这行代码的作用是清除`sip`寄存器中的SSIP位，表示“我知道了，这个软件中断我已处理”。

```c
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();
    uint64 stval = r_stval();
```
*   **作用与含义**：这是所有S模式Trap的C语言总入口。它首先读取所有相关的CSR寄存器来诊断问题：
    *   `sepc`: 保存了发生Trap时PC的值。
    *   `sstatus`: 保存了发生Trap时的特权级和中断使能状态。
    *   `scause`: **最关键的寄存器**，记录了发生Trap的原因（是中断还是异常，具体是哪一种）。
    *   `stval`: 提供了与Trap相关的附加信息（例如，缺页异常时的错误地址）。


    assert(sstatus & SSTATUS_SPP, "...");
    assert(intr_get() == 0, "...");
    
   **作用与含义**：这是两个**断言（sanity check）**，用于调试和保证正确性。
    *   第一个断言检查`sstatus`的`SPP`位，确保Trap是从S模式发生的（因为这是内核Trap处理函数）。
    *   第二个断言检查当前中断是否关闭。硬件在进入Trap时会自动关闭中断，这里是确认硬件行为符合预期。


    int trap_id = scause & 0xf; 

**作用与含义**：从`scause`寄存器中提取低4位，这4位编码了具体的异常或中断类型。

```c
    if(scause & ((uint64)1 << 63)){
```
*   **作用与含义**：检查`scause`的最高位。如果最高位是1，表示这是一个**中断（Interrupt）**；如果是0，表示这是一个**异常（Exception）**。这是区分两类事件的根本方法。

```c
        // 中断处理逻辑
        switch(trap_id){
            case 1: timer_interrupt_handler(); break;
            case 9: external_interrupt_handler(); break;
            // ...
        }
```
*   **作用与含义**：当中断发生时，根据`trap_id`分发到具体的处理函数。
    *   `case 1`: S模式软件中断，即时钟中断。
    *   `case 9`: S模式外部中断，来自PLIC。


    }
    else{
        // 异常处理逻辑
        printf("activated exception: %s\n", exception_info[trap_id]);
        // ...
        panic("kerneltrap: Exception\n");
    }
}

*   **作用与含义**：当异常发生时，这个简单的内核选择打印详细的诊断信息（异常类型、`scause`, `sepc`, `stval`的值），然后调用`panic()`函数使系统停机。在一个更复杂的操作系统中，这里会处理可恢复的异常，如缺页异常。

---

### 第三个文件：`plic.c` (假设) - PLIC驱动

这个文件是PLIC硬件的驱动程序，提供了与它交互的接口。

```c
void plic_init()
{
    *(uint32*)(PLIC_PRIORITY(UART_IRQ)) = 1;
}
```

*   **作用与含义**：全局PLIC初始化。`PLIC_PRIORITY(UART_IRQ)`是一个宏，计算出UART中断源对应的优先级寄存器的地址。这行代码向该地址写入1，**设置UART中断的优先级为1**。优先级为0的中断源是被禁用的，所以任何想要启用的中断源都必须设置一个大于0的优先级。

```c
void plic_inithart()
{ 
    int hartid = mycpuid();
```

*   **作用与含义**：获取当前CPU核心的ID。PLIC的很多寄存器是分核心的（per-hart）。

```c
    *(uint32*)PLIC_SENABLE(hartid) = (1 << UART_IRQ);
```
*   **作用与含义**：**为当前核心使能UART中断**。`PLIC_SENABLE(hartid)`宏计算出当前核心的S模式中断使能寄存器的地址。向该地址写入`(1 << UART_IRQ)`，就是将代表UART中断的那一位设置为1，告诉PLIC：“如果UART中断发生了，请通知我这个核心”。

```c
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}
```
*   **作用与含义**：**设置当前核心的中断优先级阈值**。`PLIC_SPRIORITY(hartid)`宏计算出当前核心的优先级阈值寄存器地址。向该地址写入0，意味着**任何优先级大于0的中断都可以触发当前核心**。因为我们前面把UART中断优先级设为1，所以它现在可以被正确地传递给这个核心了。

```c
int plic_claim(void)
{
    int hartid = mycpuid();
    int irq = *(uint32*)PLIC_SCLAIM(hartid);
    return irq;
}
```
*   **作用与含义**：**认领中断**。`PLIC_SCLAIM(hartid)`宏计算出当前核心的“认领/完成”寄存器地址。**读取**这个寄存器会原子地返回当前待处理的最高优先级中断的IRQ号，并自动清除该中断的挂起状态。这是中断处理的第一步。

```c
void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(uint32*)PLIC_SCLAIM(hartid) = irq;
}
```
*   **作用与含义**：**完成中断**。`PLIC_SCLAIM(hartid)`宏计算的还是那个“认领/完成”寄存器地址。**写入**之前认领的`irq`号到这个寄存器，是告诉PLIC：“我已经处理完这个中断了，你可以继续发送新的中断了”。
