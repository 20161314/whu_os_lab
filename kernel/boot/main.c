// ============================================
// 头文件包含区域
// ============================================
#include "riscv.h"           // RISC-V 架构相关定义（CSR寄存器、指令等）
#include "dev/console.h"     // 控制台输出功能
#include "dev/timer.h"       // 定时器设备驱动
#include "dev/plic.h"        // 平台级中断控制器(PLIC)
#include "lib/str.h"         // 字符串处理函数
#include "proc/proc.h"       // 进程管理相关
#include "mem/pmem.h"        // 物理内存管理
#include "mem/vmem.h"        // 虚拟内存管理
#include "trap/trap.h"       // 陷阱(trap)处理相关

// ============================================
// 全局变量声明
// ============================================
extern int interrupt_count;  // 外部定义的中断计数器，在trap_kernel.c中定义

// 全局标志变量，用于跟踪异常是否被正确处理
volatile int ebreak_handled = 0;  // EBREAK断点异常处理标志
volatile int ecall_handled = 0;   // ECALL系统调用异常处理标志

// ============================================
// 辅助函数：延时函数
// ============================================
/**
 * @brief 软件延时函数
 * @param cycles 延时的循环次数
 * 
 * 功能：通过空循环实现延时
 * 使用volatile防止编译器优化掉这个循环
 */
void delay(uint64 cycles) {
    volatile uint64 count = 0;  // volatile确保每次都从内存读取
    while (count < cycles) {
        count++;  // 简单的计数循环
    }
}

// ============================================
// 软件周期计数器（替代硬件计数器）
// ============================================
static volatile uint64 software_cycles = 0;  // 软件模拟的周期计数器

/**
 * @brief 获取CPU周期数
 * @return 当前的周期计数值
 * 
 * 功能：由于QEMU可能不支持rdcycle指令，使用软件计数器模拟
 * 每次调用增加固定值来模拟周期的流逝
 */
uint64 get_cycles(void) {
    // 方案1: 尝试使用 rdcycle 硬件指令（在某些平台可能不可用）
    // uint64 cycles;
    // __asm__ volatile ("rdcycle %0" : "=r"(cycles));
    // return cycles;
    
    // 方案2: 使用定时器的tick计数（如果timer_get_ticks()可用）
    // return timer_get_ticks();
    
    // 方案3: 使用软件计数器（当前使用的方案）
    software_cycles += 1000000;  // 每次调用增加100万，模拟周期增长
    return software_cycles;
}

// ============================================
// 测试函数1：时钟中断测试
// ============================================
/**
 * @brief 测试时钟中断功能
 * 
 * 功能：
 * 1. 等待5次时钟中断
 * 2. 记录每次中断的时间和周期数
 * 3. 计算平均每次中断的周期数
 */
void test_timer_interrupt(void) {
    // 打印测试标题
    printf("\n========================================\n");
    printf("Testing Timer Interrupt\n");
    printf("========================================\n");

    // 重置软件周期计数器，从0开始计数
    software_cycles = 0;

    // 记录测试开始时的状态
    int start_count = interrupt_count;      // 记录起始中断计数
    uint64 start_cycles = get_cycles();     // 记录起始周期数
    
    // 显示初始状态
    printf("Initial interrupt count: %d\n", start_count);
    printf("Waiting for 5 timer interrupts...\n\n");

    // 计算目标中断数：当前计数 + 5
    int target_count = start_count + 5;
    int last_count = start_count;  // 记录上一次的中断计数
    
    // 主循环：等待5次新的中断
    while (interrupt_count < target_count) {
        // 检测是否有新的中断发生
        if (interrupt_count > last_count) {
            // 有新中断发生，记录当前状态
            uint64 current_cycles = get_cycles();           // 当前周期数
            uint64 elapsed = current_cycles - start_cycles; // 已经过的周期数
            
            // 打印中断信息
            printf("Interrupt #%d received (total: %d interrupts, cycles: %d)\n", 
                   interrupt_count - start_count,  // 第几个新中断
                   interrupt_count,                // 总中断数
                   (int)elapsed);                  // 已经过的周期数
            
            last_count = interrupt_count;  // 更新上次中断计数
        }
        
        // 添加一些工作负载，模拟实际工作
        delay(50000);      // 延时50000个循环
        get_cycles();      // 更新软件计数器
    }

    // 测试结束，计算统计信息
    uint64 end_cycles = get_cycles();                    // 结束时的周期数
    uint64 elapsed_cycles = end_cycles - start_cycles;   // 总共经过的周期数
    
    // 打印测试结果
    printf("\n----------------------------------------\n");
    printf("Timer interrupt test completed!\n");
    printf("Total interrupts received: %d\n", interrupt_count - start_count);
    printf("Total cycles elapsed: %d\n", (int)elapsed_cycles);
    
    // 计算并显示平均每次中断的周期数
    if (interrupt_count - start_count > 0) {
        int cycles_per_interrupt = (int)(elapsed_cycles / (interrupt_count - start_count));
        printf("Average cycles per interrupt: %d\n", cycles_per_interrupt);
    }
    printf("========================================\n");
}

// ============================================
// 测试函数2：除零异常测试
// ============================================
/**
 * @brief 测试除零操作
 * 
 * 功能：验证RISC-V架构对除零的处理
 * 注意：RISC-V规范规定除零不触发异常，而是返回特定值
 * - 除法返回 -1
 * - 取模返回被除数本身
 */
void test_divide_by_zero(void) {
    printf("\n=== Test 1: Divide by Zero Exception ===\n");
    
    // 定义测试变量（使用volatile防止编译器优化）
    volatile int a = 100;  // 被除数
    volatile int b = 0;    // 除数（零）
    volatile int result;   // 结果
    
    printf("Attempting: %d / %d\n", a, b);
    
    // 执行除零操作
    result = a / b;
    
    // 打印结果（RISC-V应该返回-1）
    printf("Result of division by zero: %d (expected -1 on RISC-V)\n", result);
    printf("Note: RISC-V does not trap on divide by zero\n");
    
    // 测试取模操作
    result = a % b;
    printf("Result of modulo by zero: %d (expected dividend value: %d)\n", result, a);
}

// ============================================
// 测试函数3：非法指令异常测试
// ============================================
/**
 * @brief 测试非法指令异常处理
 * 
 * 功能：
 * 1. 执行一个非法指令（0x00000000）
 * 2. 触发非法指令异常
 * 3. 异常处理程序应该跳过这条指令
 * 4. 继续执行后续代码
 */
void test_illegal_instruction(void) {
    printf("\n=== Test 2: Illegal Instruction Exception ===\n");
    printf("Executing illegal instruction (0x00000000)...\n");
    
    // 内联汇编：插入非法指令
    __asm__ volatile (
        ".word 0x00000000\n"  // 插入一个全零的指令（非法）
        "nop\n"               // 添加NOP指令确保后续指令对齐
    );
    
    // 如果异常处理正确，应该能执行到这里
    printf("Returned from illegal instruction handler\n");
}

// ============================================
// 测试函数4：未对齐访问测试
// ============================================
/**
 * @brief 测试未对齐的内存访问
 * 
 * 功能：
 * 1. 创建一个对齐的缓冲区
 * 2. 尝试从非对齐地址读取64位数据
 * 3. 某些RISC-V实现会在硬件中处理未对齐访问
 * 4. 某些实现会触发异常
 */
void test_misaligned_access(void) {
    printf("\n=== Test 3: Misaligned Load/Store Exception ===\n");
    
    // 创建一个8字节对齐的缓冲区
    char buffer[16] __attribute__((aligned(8)));
    
    // 创建一个未对齐的指针（偏移1字节）
    uint64 *misaligned_ptr = (uint64 *)(buffer + 1);
    
    printf("Attempting misaligned 64-bit load at address: %p\n", misaligned_ptr);
    
    // 尝试从未对齐地址读取64位数据
    volatile uint64 value = *misaligned_ptr;
    
    // 打印结果
    printf("Misaligned load completed, value: 0x%x%x\n", 
           (uint32)(value >> 32), (uint32)value);
    printf("Note: Some RISC-V implementations handle misaligned access in hardware\n");
}

// ============================================
// 测试函数5：非法内存访问测试
// ============================================
/**
 * @brief 测试访问NULL指针（页错误）
 * 
 * 功能：
 * 1. 尝试读取NULL指针
 * 2. 应该触发Load Page Fault异常
 * 3. 异常处理程序会调用panic终止系统
 */
void test_invalid_memory_access(void) {
    printf("\n=== Test 4: Invalid Memory Access Exception ===\n");
    
    printf("Attempting to read from NULL pointer...\n");
    
    // 创建NULL指针
    volatile int *null_ptr = NULL;
    
    // 尝试读取NULL指针（会触发页错误）
    volatile int value = *null_ptr;
    
    // 如果到达这里说明异常处理有问题
    printf("Read value: %d (should not reach here)\n", value);
}

// ============================================
// 测试函数6：特权级违规测试
// ============================================
/**
 * @brief 测试访问内核地址空间
 * 
 * 功能：
 * 1. 尝试访问内核地址空间
 * 2. 如果在用户态应该触发异常
 * 3. 如果在内核态可能可以访问
 */
void test_privilege_violation(void) {
    printf("\n=== Test 5: Privilege Violation (Access Kernel Memory) ===\n");
    
    // 内核地址空间的起始地址
    volatile uint64 *kernel_addr = (uint64 *)0xFFFFFFFF80000000UL;
    
    printf("Attempting to access kernel address: %p\n", kernel_addr);
    
    // 尝试读取内核地址
    volatile uint64 value = *kernel_addr;
    
    printf("Read value: 0x%x%x (should not reach here if in user mode)\n",
           (uint32)(value >> 32), (uint32)value);
}

// ============================================
// 测试函数7：ECALL系统调用测试
// ============================================
/**
 * @brief 测试ECALL指令（系统调用）
 * 
 * 功能：
 * 1. 执行ECALL指令
 * 2. 触发Environment Call异常
 * 3. 异常处理程序应该处理这个调用
 * 4. 设置ecall_handled标志
 * 5. 返回到调用点的下一条指令
 */
void test_ecall(void) {
    printf("\n=== Test 6: ECALL (Environment Call) ===\n");
    printf("Executing ECALL instruction...\n");
    printf("This should trigger an Environment Call exception (cause=8/9/11)\n");
    
    // 重置标志
    ecall_handled = 0;
    
    // 执行ECALL指令
    __asm__ volatile (
        "li a7, 1\n"    // 设置系统调用号为1（存入a7寄存器）
        "li a0, 42\n"   // 设置参数为42（存入a0寄存器）
        "ecall\n"       // 执行系统调用指令
    );
    
    // 检查是否被正确处理
    if (ecall_handled) {
        printf("✓ ECALL was properly handled\n");
    } else {
        printf("✗ ECALL was not handled\n");
    }
    
    printf("Returned from ECALL\n");
}

// ============================================
// 测试函数8：EBREAK断点测试
// ============================================
/**
 * @brief 测试EBREAK指令（断点）
 * 
 * 功能：
 * 1. 记录执行EBREAK前的PC值
 * 2. 执行EBREAK指令触发断点异常
 * 3. 异常处理程序跳过EBREAK指令
 * 4. 记录返回后的PC值
 * 5. 验证PC正确前进了2字节（EBREAK是压缩指令）
 */
void test_ebreak(void) {
    printf("\n=== Test 7: EBREAK (Breakpoint) ===\n");
    printf("Executing EBREAK instruction...\n");
    printf("This should trigger a Breakpoint exception (cause=3)\n");
    
    // 重置标志
    ebreak_handled = 0;
    
    // 获取当前PC值（使用auipc指令）
    uint64 pc_before;
    __asm__ volatile ("auipc %0, 0" : "=r"(pc_before));
    printf("PC before EBREAK: 0x%x%x\n", 
           (uint32)(pc_before >> 32), (uint32)pc_before);
    
    printf("Executing EBREAK now...\n");
    
    // 执行EBREAK断点指令
    __asm__ volatile ("ebreak");
    
    // 获取返回后的PC值
    uint64 pc_after;
    __asm__ volatile ("auipc %0, 0" : "=r"(pc_after));
    printf("PC after EBREAK: 0x%x%x\n", 
           (uint32)(pc_after >> 32), (uint32)pc_after);
    
    // 检查是否被正确处理
    if (ebreak_handled) {
        printf("✓ EBREAK was properly handled by exception handler\n");
    } else {
        printf("✗ EBREAK was not handled (or handler didn't set flag)\n");
    }
    
    printf("Successfully returned from EBREAK handler!\n");
    printf("EBREAK test completed\n");
}

// ============================================
// 测试函数9：多次EBREAK测试
// ============================================
/**
 * @brief 测试多次执行EBREAK
 * 
 * 功能：
 * 1. 连续执行3次EBREAK
 * 2. 验证每次都能正确处理
 * 3. 确保异常处理程序是可重入的
 */
void test_ebreak_multiple(void) {
    printf("\n=== Test 7b: Multiple EBREAK Tests ===\n");
    
    // 循环执行3次EBREAK测试
    for (int i = 1; i <= 3; i++) {
        printf("\nEBREAK test iteration %d:\n", i);
        
        // 重置标志
        ebreak_handled = 0;
        
        // 执行EBREAK
        __asm__ volatile ("ebreak");
        
        // 检查结果
        if (ebreak_handled) {
            printf("  ✓ Iteration %d: EBREAK handled\n", i);
        } else {
            printf("  ✗ Iteration %d: EBREAK not handled\n", i);
        }
    }
    
    printf("\nMultiple EBREAK tests completed\n");
}

// ============================================
// 综合异常测试函数
// ============================================
/**
 * @brief 执行所有异常处理测试
 * 
 * 功能：按顺序执行所有异常测试用例
 * 注意：某些测试（如页错误）会导致系统panic，因此被注释掉
 */
void test_exception_handler(void) {
    printf("\n========================================\n");
    printf("Starting Exception Handler Tests\n");
    printf("========================================\n");

    // 测试1: 除零（RISC-V不会触发异常）
    test_divide_by_zero();
    
    // 测试2: 非法指令（会触发异常）
    test_illegal_instruction();
    
    // 测试3: 未对齐访问
    test_misaligned_access();
    
    // 测试4: EBREAK断点
    test_ebreak();
    
    // 测试5: 多次EBREAK
    test_ebreak_multiple();
    
    // 测试6: ECALL系统调用
    test_ecall();
    
    // 以下测试会导致严重异常，建议在异常处理完善后再测试
    // test_invalid_memory_access();   // 会触发页错误并panic
    // test_privilege_violation();      // 可能触发访问违规

    printf("\n========================================\n");
    printf("Exception tests completed\n");
    printf("========================================\n");
}

// ============================================
// 主函数：系统入口点
// ============================================
/**
 * @brief 内核主函数
 * 
 * 功能：
 * 1. 初始化所有硬件和子系统
 * 2. 启动中断
 * 3. 执行测试程序
 * 4. 进入无限循环
 */
void main()
{
    // ========== 系统初始化阶段 ==========
    
    uart_init();              // 初始化UART串口（用于printf输出）
    pmem_init();              // 初始化物理内存管理器
    kvm_init();               // 初始化内核虚拟内存
    kvm_inithart();           // 初始化当前CPU核心的虚拟内存
    trap_kernel_init();       // 初始化陷阱处理系统（全局部分）
    trap_kernel_inithart();   // 初始化当前核心的陷阱处理
    plic_init();              // 初始化平台级中断控制器（全局部分）
    plic_inithart();          // 初始化当前核心的PLIC

    // ========== 打印启动信息 ==========
    
    printf("\n");
    printf("========================================\n");
    printf("        WHU Operating System Lab        \n");
    printf("========================================\n");
    printf("System initialized successfully\n");
    printf("Starting tests...\n");

    // ========== 启动中断 ==========
    
    intr_on();  // 开启中断（设置sstatus.SIE位）

    // ========== 执行测试程序 ==========
    
    // 测试1: 时钟中断功能
    test_timer_interrupt();
    
    // 测试2: 各种异常处理
    test_exception_handler();
    
    // 测试3: 页错误（会导致panic）
    printf("\n========================================\n");
    printf("Testing Invalid Memory Access\n");
    printf("WARNING: This will trigger a page fault!\n");
    printf("========================================\n");
    
    test_invalid_memory_access();  // 这个测试会触发页错误并终止系统
    
    // ========== 理论上不会到达这里 ==========
    
    // 如果异常处理正确，上面的test_invalid_memory_access会导致panic
    // 所以下面的代码不应该被执行
    printf("\n========================================\n");
    printf("All tests completed successfully!\n");
    printf("========================================\n");

    // ========== 无限循环 ==========
    
    // 内核不应该退出，进入无限循环等待中断
    while(1);
}
