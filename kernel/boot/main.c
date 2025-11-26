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

    while(1);
}