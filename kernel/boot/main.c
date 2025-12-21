#include "riscv.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/shm.h"
#include "trap/trap.h"

void main()
{
    // clear_screen();

    print_init();
    pmem_init();
    kvm_init();
    kvm_inithart();
    shm_init();
    proc_init();
    trap_kernel_init();
    trap_kernel_inithart();
    plic_init();
    plic_inithart();
    // 第一个用户进程，将会切换到initcode中的地址，测试时不启用
    proc_make_first(); 

    printf("Hello OS\n");

    proc_scheduler();
}