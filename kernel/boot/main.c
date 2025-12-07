#include "riscv.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "fs/fs.h"
#include "fs/file.h"

void main()
{
    // clear_screen();

    print_init();
    pmem_init();
    kvm_init();
    kvm_inithart();
    proc_init();
    trap_kernel_init();
    trap_kernel_inithart();
    plic_init();
    plic_inithart();
    
    // 初始化文件系统
    printf("About to call fs_init...\n");
    fs_init(ROOTDEV);
    printf("fs_init returned.\n");
    
    // 初始化文件表
    file_init();
    printf("File system initialized.\n");
    printf("Hello OS\n");

    proc_make_first();

    proc_scheduler();
}