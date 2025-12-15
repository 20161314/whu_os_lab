## syscall.c

这段代码实现了操作系统内核中**系统调用的分发（Dispatch）与参数获取机制**。

当用户程序执行 `ecall` 指令陷入内核时，内核的陷阱处理程序（Trap Handler）会调用这里的 `syscall()` 函数。它的主要任务是：根据寄存器中的系统调用号，找到并执行对应的内核函数，然后将结果返回给用户。

下面我将分模块详细解读每一行代码。

### 1. 头文件与函数指针表

```c
#include "dev/console.h"    // 用于 printf 输出
#include "proc/cpu.h"       // 用于 myproc() 获取当前进程
#include "mem/vmem.h"       // 用于 uvm_copyin_str 内存复制
#include "syscall/syscall.h" // 系统调用相关定义
#include "syscall/sysnum.h"  // 系统调用号定义 (如 SYS_fork = 1)
#include "syscall/sysfunc.h" // 具体系统调用函数的声明 (如 sys_fork)

// 系统调用跳转表 (Function Pointer Array)
// 这是一个函数指针数组，索引是系统调用号，值是对应的函数地址
static uint64 (*syscalls[])(void) = {
    [SYS_test]          sys_test,   // 映射 SYS_test 号到 sys_test 函数
    [SYS_print]         sys_print,
    [SYS_brk]           sys_brk,
    [SYS_mmap]          sys_mmap,
    [SYS_munmap]        sys_munmap,
    [SYS_fork]          sys_fork,
    [SYS_wait]          sys_wait,
    [SYS_exit]          sys_exit,
    [SYS_sleep]         sys_sleep,
    [SYS_kill]          sys_kill,
    [SYS_getpid]        sys_getpid,
};

// 宏定义：计算数组元素的个数
// sizeof(x) 是总字节数，sizeof((x)[0]) 是单个元素字节数
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
```

### 2. 系统调用分发器 (`syscall`)

这是系统调用的**总入口**。

```c
void syscall()
{
  int num;
  struct proc *p = myproc(); // 获取当前正在 CPU 上运行的进程结构体

  // 1. 获取系统调用号
  // 在 RISC-V 架构约定中，用户程序将系统调用号存放在 a7 寄存器中
  // 当发生 trap 时，寄存器被保存在 p->tf (Trapframe) 中
  num = p->tf->a7;

  // 2. 检查系统调用号是否合法
  // 必须大于等于0，且小于数组长度，且对应的函数指针不为空
  if(num >= 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 3. 执行系统调用
    // syscalls[num]() 调用对应的内核函数
    // 4. 保存返回值
    // RISC-V 约定：函数返回值存放在 a0 寄存器中
    // 我们修改 trapframe 中的 a0，这样当从内核返回用户态时，用户就能在 a0 拿到结果
    p->tf->a0 = syscalls[num]();
  } else {
    // 如果系统调用号未知，打印错误信息
    printf("proc %d : unknown sys call %d\n",
            p->pid, num);
    // 返回 -1 表示出错
    p->tf->a0 = -1;
  }
}
```

### 3. 参数获取机制 (`arg_raw`)

内核函数（如 `sys_kill`）本身没有参数定义（`void`），因为参数不在内核栈上，而在**被中断的用户进程的寄存器里**。我们需要去 `trapframe` 里取。

```c
/*
    其他用于读取传入参数的函数
    参数分为两种,第一种是数据本身,第二种是指针
    第一种使用tf->ax传递
    第二种使用uvm_copyin 和 uvm_copyinstr 进行传递
*/

// 核心辅助函数：读取第 n 个参数的原始值
// RISC-V 约定：函数参数依次存放在 a0, a1, ..., a5 寄存器中
static uint64 arg_raw(int n)
{   
    proc_t* proc = myproc();
    switch(n) {
        case 0:
            return proc->tf->a0; // 第 0 个参数在 a0
        case 1:
            return proc->tf->a1; // 第 1 个参数在 a1
        case 2:
            return proc->tf->a2;
        case 3:
            return proc->tf->a3;
        case 4:
            return proc->tf->a4;
        case 5:        
            return proc->tf->a5;
        default:
            panic("arg_raw: illegal arg num"); // 只支持 6 个参数
            return -1;
    }
}
```

### 4. 类型化参数读取 (`arg_int`, `arg_str` 等)

这些函数是对 `arg_raw` 的封装，根据需要的类型进行转换或内存拷贝。

```c
// 读取 int 类型的参数
// n: 第几个参数
// ip: 内核中用于存放结果的指针
void arg_int(int n, int* ip)
{
    *ip = arg_raw(n); // 读取寄存器值，隐式转换为 int
}

// 读取 uint32 类型的参数
void arg_uint32(int n, uint32* ip)
{
    *ip = arg_raw(n);
}

// 读取 uint64 类型的参数（也是指针类型，因为指针是64位的）
void arg_uint64(int n, uint64* ip)
{
    *ip = arg_raw(n);
}

// 读取字符串参数
// 这是最复杂的，因为寄存器里存的是“用户空间的虚拟地址”
// 内核不能直接解引用这个地址，必须查页表并拷贝数据
void arg_str(int n, char* buf, int maxlen)
{
    proc_t* p = myproc();
    uint64 addr;
    
    // 1. 先从寄存器里拿到字符串在用户空间的起始地址
    arg_uint64(n, &addr);

    // 2. 从用户空间拷贝字符串到内核空间
    // p->pgtbl: 当前进程的页表（用于虚拟地址翻译）
    // (uint64)buf: 内核缓冲区的地址
    // addr: 用户字符串的地址
    // maxlen: 最大读取长度
    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, maxlen);
}
```

### 总结
这个文件 (`syscall.c`) 是**内核态与用户态数据交互的枢纽**：
1.  **控制流交互**：`syscall()` 函数根据 `a7` 寄存器决定执行哪个内核功能。
2.  **数据流交互**：
    *   **输入**：通过 `arg_*` 系列函数，从 `trapframe` 的 `a0-a5` 寄存器中提取用户传入的参数。如果是指针，还需要通过 `uvm_copy` 系列函数跨越地址空间边界拷贝数据。
    *   **输出**：将执行结果写入 `trapframe` 的 `a0` 寄存器，以便 `sret` 指令返回用户态后，用户程序能获取返回值。

## sysfunc.c

这段代码是操作系统内核中**系统调用（System Call）的具体实现层**。

系统调用是用户程序请求内核服务的唯一接口。当用户程序执行 `ecall` 指令后，内核会捕获异常，解析系统调用号，并分发到这里定义的各个 `sys_` 函数中执行。

这些函数的主要职责是：
1.  **参数获取**：从用户空间的寄存器或栈中提取参数（使用 `arg_int`, `arg_uint64`, `arg_str` 等辅助函数）。
2.  **逻辑处理**：调用内核其他子系统（如内存管理、进程管理）的功能。
3.  **返回值**：将结果返回给用户进程。

下面详细解读每个系统调用：

### 1. 基础调试功能 (`sys_test`, `sys_print`)

```c
// 测试系统调用
// 功能：仅用于验证系统调用机制是否通畅。
uint64 sys_test()
{
    printf(".\n"); // 在内核控制台打印一个点
    return 0;      // 返回成功
}

// 打印字符串系统调用
// 功能：允许用户进程向内核控制台输出字符串。
uint64 sys_print()
{
    char* message = "";
    // 从用户空间的第 0 个参数获取字符串，最大长度限制为 256
    // arg_str 会处理用户指针到内核指针的转换及安全检查
    arg_str(0, message, 256);
    printf(message); // 调用内核的 printf 输出
    return 0;
}
```

---

### 2. 内存管理 (`sys_brk`, `sys_mmap`, `sys_munmap`)

**`sys_brk`** 是最经典的内存分配系统调用，用于调整进程堆（Heap）的大小。

```c
uint64 sys_brk()
{
    uint64 new_heap_top; // 用户请求的新堆顶地址
    uint64 old_heap_top; // 当前的堆顶地址
    uint64 res;
    pgtbl_t pgtbl = myproc()->pgtbl; // 获取当前进程的页表

    // 获取第 0 个参数：新的堆顶地址
    arg_uint64(0, &new_heap_top);
    
    // 获取当前进程结构体中记录的堆顶
    old_heap_top = myproc()->heap_top;

    // 情况 1: 参数为 0，表示查询当前堆顶位置（sbrk(0) 的用法）
    if(new_heap_top == 0){
        return old_heap_top;
    }
    // 情况 2: 新堆顶 > 旧堆顶，表示申请更多内存（增长堆）
    else if(new_heap_top > old_heap_top){
        // 调用内存管理模块，分配物理页并映射
        // 参数：页表，旧地址，增长量
        res = uvm_heap_grow(pgtbl, old_heap_top, new_heap_top - old_heap_top);
        return res; // 返回新的堆顶（或者失败码）
    }
    // 情况 3: 新堆顶 < 旧堆顶，表示释放内存（收缩堆）
    else{
        // 调用内存管理模块，取消映射并释放物理页
        res = uvm_heap_ungrow(pgtbl, old_heap_top, old_heap_top - new_heap_top);
        return res;
    }
}

// 内存映射（未实现）
// 通常用于加载动态库或映射文件
uint64 sys_mmap()
{
    return -1; // 直接返回错误
}

// 取消内存映射（未实现）
uint64 sys_munmap()
{
    return -1;
}
```

---

### 3. 进程管理 (`sys_fork`, `sys_wait`, `sys_exit`)

这些是 Unix/Linux 进程模型的核心系统调用。

```c
// 创建子进程
// 功能：复制当前进程，创建一个几乎完全一样的新进程。
uint64 sys_fork()
{
    // 直接调用进程管理层的 proc_fork()
    // 父进程返回子进程 PID，子进程返回 0
    return proc_fork();
}

// 等待子进程退出
// 功能：回收僵尸子进程（Zombie）的资源，获取其退出状态。
uint64 sys_wait()
{
    uint64 p;
    // 获取参数：一个用户空间的指针，用于存放子进程的退出状态码
    arg_uint64(0, &p);
    // 调用 proc_wait，阻塞直到有子进程退出
    return proc_wait(p);
}

// 进程退出
// 功能：终止当前进程，释放资源，变为僵尸状态。
uint64 sys_exit()
{
    int n;
    // 获取退出状态码（如 exit(0) 中的 0）
    arg_int(0, &n);
    proc_exit(n); // 执行退出逻辑，唤醒父进程
    return 0;  // 这行代码永远不会执行，因为进程已经停止运行并调度出去了
}
```

---

### 4. 信号与时间 (`sys_sleep`, `sys_kill`, `sys_getpid`)

```c
// 进程睡眠
// 功能：让进程暂停执行指定的时钟滴答数（ticks）。
uint64 sys_sleep()
{
    int n;          // 睡眠时长
    uint64 ticks0;  // 开始睡眠时的系统时间

    arg_int(0, &n); // 获取参数

    // 获取定时器锁
    // 必须持有锁才能读取全局 tick 计数，并防止丢失唤醒信号
    spinlock_acquire(&timer_get()->lk);
    
    ticks0 = timer_get_ticks(); // 记录当前时间
    
    // 循环等待，直到流逝的时间 >= n
    while(timer_get_ticks() - ticks0 < n){
        // 检查进程是否被 kill 信号标记
        if(proc_killed(myproc())){
            spinlock_release(&timer_get()->lk);
            return -1; // 如果被杀，提前返回错误，不再睡眠
        }
        
        // 核心操作：原子地释放锁并进入睡眠状态
        // 进程状态变为 SLEEPING，让出 CPU
        // 当时钟中断发生并更新 ticks 时，会唤醒在此等待的进程
        proc_sleep(&timer_get()->ticks, &timer_get()->lk);
    }
    
    spinlock_release(&timer_get()->lk); // 醒来后释放锁
    return 0;
}

// 杀死进程
// 功能：向指定 PID 的进程发送终止信号（设置 killed 标志）。
uint64 sys_kill()
{
    int pid;
    arg_int(0, &pid);
    return proc_kill(pid); // 并不立即杀死，只是标记，目标进程下次进入内核时会退出
}

// 获取当前进程 ID
uint64 sys_getpid()
{
    return myproc()->pid; // 从当前 CPU 的进程结构中读取 PID
}
```

### 总结
这个文件 (`sysfunc.c`) 是内核与用户空间的**桥梁**。
1.  **接口层**：它不实现复杂的算法（如页表映射算法、调度算法），而是调用下层模块（`uvm`, `proc`, `timer`）来完成工作。
2.  **安全性**：它负责从不可信的用户空间安全地读取参数（`arg_*` 函数）。
3.  **功能覆盖**：涵盖了最基本的操作系统功能：内存申请 (`brk`)、进程控制 (`fork/exec/wait`)、时间控制 (`sleep`)。

## 入口所在：trap_user_handler

这段代码是操作系统内核中**处理用户态 Trap（中断和异常）的核心入口函数**。

当 CPU 处于用户模式（U-mode）运行时，如果发生了中断（如时钟中断）或异常（如系统调用 `ecall`、非法指令、缺页），硬件会自动跳转到汇编层面的入口（`user_vector`），保存寄存器后，就会调用这个 C 语言函数 `trap_user_handler`。

它的主要职责是：**识别 Trap 类型 -> 分发处理 -> 准备返回用户态**。

下面是逐行详细解析：

### 1. 读取硬件状态寄存器 (CSR)

```c
void trap_user_handler()
{
    // 读取 SEPC (Supervisor Exception Program Counter)
    // 记录了发生 Trap 时，用户程序执行到了哪条指令的地址
    uint64 sepc = r_sepc();          

    // 读取 SSTATUS (Supervisor Status)
    // 包含之前的特权模式(SPP)、中断使能状态(SPIE)等信息
    uint64 sstatus = r_sstatus();    

    // 读取 SCAUSE (Supervisor Cause)
    // 这是一个数字代码，告诉我们需要处理的是什么（例如：8代表系统调用，1代表时钟中断）
    uint64 scause = r_scause();      

    // 读取 STVAL (Supervisor Trap Value)
    // 包含 Trap 的附加信息（例如：如果是访存错误，这里存的是错误的内存地址）
    uint64 stval = r_stval();        
    
    // 获取当前 CPU 上正在运行的进程结构体
    proc_t* p = myproc();
```

### 2. 安全检查与环境切换

```c
    // 断言检查：确保 Trap 确实是来自用户模式 (U-mode)
    // SSTATUS_SPP 位记录了进入 Trap 之前的特权级。如果为 0，说明之前是 User Mode。
    // 如果这里失败，说明内核逻辑有严重错误（比如在内核态错误地使用了用户态的中断向量）。
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    // 关键步骤：切换中断向量表
    // 因为我们现在已经进入了内核模式，如果此时再次发生中断（嵌套中断），
    // 必须由内核的 Trap 处理器 (kernel_vector) 来接管，而不是用户的。
    w_stvec((uint64)kernel_vector);

    // 保存用户程序的 PC 到进程结构体的 TrapFrame 中
    // 因为后续可能会发生进程切换 (yield)，sepc 寄存器会被覆盖，所以必须保存到内存里。
    p->tf->epc = sepc;
```

### 3. 解析 Trap 类型

```c
    // 解析 scause 寄存器
    // 低位部分是 Trap ID
    int trap_id = scause & 0xf;
    
    // 最高位（第63位）决定是“中断(Interrupt)”还是“异常(Exception)”
    // 1 = 中断 (异步，如时钟、键盘)
    // 0 = 异常 (同步，如系统调用、除零错误)
    bool isInterrupt = ((scause & ((uint64)1 << 63)) != 0);
    
    // 获取调试用的字符串信息
    char* info = isInterrupt ? interrupt_info[trap_id] : exception_info[trap_id];
```

### 4. 处理系统调用 (System Call)

这是最常见的路径，当用户程序执行 `ecall` 指令时触发。

```c
    if(scause == 8){
        // scause 为 8 代表 "Environment call from U-mode" (系统调用)

        // 检查进程是否已被标记为杀死，如果是则直接退出，不执行系统调用
        if(proc_killed(p)) proc_exit(-1);

        // ！！！非常关键的一步！！！
        // sepc 指向的是触发 Trap 的那条指令（即 ecall 指令）。
        // 如果我们处理完直接返回 sepc，CPU 会再次执行 ecall，导致死循环。
        // 所以必须将 PC + 4（一条指令的长度），让它返回后执行 ecall 后面的下一条指令。
        p->tf->epc += 4;

        // 开启中断
        // 系统调用可能耗时较长（如读写磁盘），为了不阻塞时钟中断和其他设备中断，
        // 在执行具体的 syscall 函数前，允许内核响应中断。
        intr_on();

        // 调用系统调用分发器 (见上一个文件分析)
        syscall();
```

### 5. 处理中断 (Interrupts)

```c
    } else if (isInterrupt) {
        switch (trap_id)
        {
        case 1:
            // 处理时钟中断 (Supervisor Software Interrupt / Timer Interrupt)
            // 更新系统时间 tick
            timer_interrupt_handler();
            // ！！！关键！！！
            // 时间片用完，主动让出 CPU (Yield)，触发进程调度
            proc_yield();
            break;
        case 9:
            // 处理外部中断 (Supervisor External Interrupt)
            // 例如：键盘输入、UART 串口数据、磁盘完成信号
            external_interrupt_handler();
            break;
        default:
            // 未知中断，打印错误信息并 Panic
            printf("usertrap(): unexpected interrupt at pid=%d\n", p->pid);
            // ... 打印调试信息 ...
            panic("usertrap: unexpected interrupt");
            break;
        }
```

### 6. 处理异常 (Exceptions)

```c
    } else {
        // 处理其他异常（非系统调用）
        // 例如：非法指令、除以零、访问非法内存地址
        // 在这个简单的实现中，直接打印错误并 Panic (或者杀死进程)
        printf("usertrap(): exception at pid=%d\n", p->pid);
        printf("            trap id: %d trap info: %s\n", trap_id, info);
        printf("            scause %p\n", scause);
        printf("            sepc=%p stval=%p\n", sepc, stval);
        
        panic("usertrap: unexpected exception");
        // 实际的 OS 这里通常会发送信号 (Signal) 或者杀死进程 (Segmentation Fault)，而不是 Panic 整个系统
        proc_setkilled(p);
    }
```

### 7. 返回用户态

```c
    // 再次检查进程是否被杀死
    // 因为在处理中断或系统调用期间，可能有其他进程 kill 了当前进程
    if(proc_killed(p)) proc_exit(-1);

    // 调用返回函数
    // 这个函数会恢复寄存器，切换回 user_vector，并执行 sret 指令回到用户空间
    trap_user_return();
}
```

### 总结
`trap_user_handler` 是内核的**交通枢纽**：
1.  **上下文保护**：将硬件寄存器 (`sepc`) 备份到内存 (`trapframe`)。
2.  **模式切换**：将 Trap 向量表切换为内核模式 (`kernel_vector`) 以支持内核态中断。
3.  **路由分发**：
    *   如果是 **Syscall (8)** -> 修改 PC，开中断，执行功能。
    *   如果是 **Timer (1)** -> 调度 (`yield`)。
    *   如果是 **Device (9)** -> 处理硬件 I/O。
    *   如果是 **Error** -> 报错或杀进程。