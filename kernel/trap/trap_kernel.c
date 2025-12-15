#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
static char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
static char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 全局变量用于跟踪异常处理
extern volatile int ebreak_handled;
extern volatile int ecall_handled;

// 中断计数器
int interrupt_count = 0;

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    timer_create();
    interrupt_count = 0;  // 重置中断计数
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    w_stvec((uint64)kernel_vector);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    // irq indicates which device interrupted.
    int irq = plic_claim();

    switch(irq){
        case 0:
            break;
        case UART_IRQ:
            uart_intr();
            break;
        case VIRTIO0_IRQ:
            // virtio_disk_intr();
            break;
        default:
            printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
        plic_complete(irq);
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // 输出当前ticks的测试代码部分
    // int ticks = timer_get_ticks();
    // printf("Current tick is %d.\n", ticks);

    if(mycpuid() == 0){
        timer_update();
    }
    // 通过清除SSIP位，承认软件中断
    w_sip(r_sip() & ~2);
}

// 处理 EBREAK 异常
void handle_breakpoint(uint64 sepc)
{
    printf("\n[TRAP] Breakpoint exception at PC: 0x%x%x\n",
           (uint32)(sepc >> 32), (uint32)sepc);
    
    // 设置全局标志
    ebreak_handled = 1;
    
    // 需要跳过 EBREAK 指令
    // RISC-V 中 EBREAK 可能是 2 字节(压缩)或 4 字节(标准)
    uint16 *instr = (uint16 *)sepc;
    
    // 检查指令长度
    // 如果最低两位是 11, 则是 32 位指令
    if ((*instr & 0x3) == 0x3) {
        // 32位指令
        w_sepc(sepc + 4);
        printf("[TRAP] Skipping 4-byte EBREAK instruction\n");
    } else {
        // 16位压缩指令
        w_sepc(sepc + 2);
        printf("[TRAP] Skipping 2-byte EBREAK instruction\n");
    }
    
    printf("[TRAP] Returning from breakpoint handler\n");
}

// 处理 ECALL 异常
void handle_ecall(uint64 sepc)
{
    printf("\n[TRAP] Environment call (ECALL) at PC: 0x%x%x\n",
           (uint32)(sepc >> 32), (uint32)(sepc));
    
    // 设置全局标志
    ecall_handled = 1;
    
    // 读取系统调用参数 (如果需要)
    // a7 通常是系统调用号, a0-a5 是参数
    // 这些值在 trapframe 中
    
    // ECALL 指令总是 4 字节
    w_sepc(sepc + 4);
    
    printf("[TRAP] ECALL handled, returning\n");
}

// 处理非法指令异常
void handle_illegal_instruction(uint64 sepc, uint64 stval)
{
    printf("\n[TRAP] Illegal instruction at PC: 0x%x%x\n",
           (uint32)(sepc >> 32), (uint32)(sepc));
    printf("[TRAP] Instruction value (stval): 0x%x%x\n",
           (uint32)(stval >> 32), (uint32)stval);
    
    // 读取实际的指令
    uint32 *instr_ptr = (uint32 *)sepc;
    uint32 instr = *instr_ptr;
    printf("[TRAP] Instruction at fault address: 0x%x\n", instr);
    
    // 检查指令长度并跳过
    uint16 *instr16 = (uint16 *)sepc;
    if ((*instr16 & 0x3) == 0x3) {
        // 32位指令
        w_sepc(sepc + 4);
        printf("[TRAP] Skipping 4-byte illegal instruction\n");
    } else {
        // 16位压缩指令
        w_sepc(sepc + 2);
        printf("[TRAP] Skipping 2-byte illegal instruction\n");
    }
    
    printf("[TRAP] Continuing execution after illegal instruction\n");
}

// 处理页错误
void handle_page_fault(uint64 sepc, uint64 stval, int fault_type)
{
    const char *fault_names[] = {
        "Instruction page fault",
        "Load page fault", 
        "Store/AMO page fault"
    };
    
    int fault_index = (fault_type == 12) ? 0 : (fault_type == 13) ? 1 : 2;
    
    printf("\n[TRAP] %s\n", fault_names[fault_index]);
    printf("[TRAP] Fault address: 0x%x%x\n",
           (uint32)(stval >> 32), (uint32)stval);
    printf("[TRAP] PC at fault: 0x%x%x\n",
           (uint32)(sepc >> 32), (uint32)sepc);
    
    // 对于测试,我们可以选择:
    // 1. panic (当前行为)
    // 2. 返回特定值
    // 3. 终止当前任务
    
    printf("[TRAP] Page fault cannot be recovered, panicking...\n");
    panic("Page fault");
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf; 

    // 中断异常处理核心逻辑
    if(scause & ((uint64)1 << 63)){
        // 中断处理
        interrupt_count++;  // 只在中断时增加计数
        
        switch(trap_id){
            case 1:
                timer_interrupt_handler();
                break;
            case 9:
                external_interrupt_handler();
                break;
            default:
                printf("activated interrupt: %s\n", interrupt_info[trap_id]);
                printf("scause %p\n", scause);
                printf("sepc=%p stval=%p\n", sepc, stval);
                panic("kerneltrap: not recognized");
                break;
        }
    }
    else{
        // 异常处理
        printf("\n========================================\n");
        printf("Exception caught: %s\n", exception_info[trap_id]);
        printf("scause: 0x%x\n", (uint32)scause);
        printf("sepc:   0x%x%x\n", (uint32)(sepc >> 32), (uint32)sepc);
        printf("stval:  0x%x%x\n", (uint32)(stval >> 32), (uint32)stval);
        printf("========================================\n");
        
        switch(trap_id){
            case 2:  // Illegal instruction
                handle_illegal_instruction(sepc, stval);
                break;
                
            case 3:  // Breakpoint (EBREAK)
                handle_breakpoint(sepc);
                break;
                
            case 8:  // ECALL from U-mode
            case 9:  // ECALL from S-mode
            case 11: // ECALL from M-mode
                handle_ecall(sepc);
                break;
                
            case 12: // Instruction page fault
            case 13: // Load page fault
            case 15: // Store/AMO page fault
                handle_page_fault(sepc, stval, trap_id);
                break;
                
            case 0:  // Instruction address misaligned
            case 1:  // Instruction access fault
            case 4:  // Load address misaligned
            case 5:  // Load access fault
            case 6:  // Store/AMO address misaligned
            case 7:  // Store/AMO access fault
            default:
                printf("[TRAP] Unhandled exception, panicking...\n");
                panic("kerneltrap: Exception");
                break;
        }
    }
}
