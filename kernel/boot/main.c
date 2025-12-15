#include "riscv.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"

extern int interrupt_count;

void test_timer_interrupt(void) {
    printf("Testing timer interrupt...\n");

    // 记录中断前的时间
    uint64 start_time = timer_get_ticks();

    // 在时钟中断处理函数中增加计数
    // 等待几次中断
    int x = 0;
    while (interrupt_count < 5) {
        // 可以在这里执行其他任务
        printf("Waiting for interrupt %d...\n", interrupt_count + 1);

        // 简单延时
        for (int i = 0; i < 1000000; i++){
            for(int j = 0; j < 1000000; j++){
                x += 1;
            }
            x -= 999999;
        }
    }

    uint64 end_time = timer_get_ticks();

    printf("Timer test completed: %d interrupts in %d cycles\n", 
           interrupt_count, end_time - start_time);
    printf("x = %d\n" , x);
}

int func(int x){
    return x - x;
}

void test_exception_handler(void) {
    printf("Testing exception handling... \n");

    // 警告：本测试会导致panic，因为异常没有被真正的处理。

    // 测试除零异常（如果支持）
    int a = 5 / func(5);

    printf("%d\n", a);

    // 测试非法指令异常
    
    // 测试内存访问异常
    vm_getpte(NULL, a, false);

    printf("Exception tests completed \n");
}



void main()
{
    // clear_screen();

    uart_init();
    pmem_init();
    kvm_init();
    kvm_inithart();
    trap_kernel_init();
    trap_kernel_inithart();
    plic_init();
    plic_inithart();

    printf("Hello OS\n");

    intr_on();

    test_timer_interrupt();
    test_exception_handler();

    while(1);
}