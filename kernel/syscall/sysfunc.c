#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "lib/str.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "riscv.h"

// 系统调用测试（无功能）
// 输出一行调试信息，然后返回0
uint64 sys_test()
{
    printf(".\n");
    return 0;
}

// 输出对应的信息
// char* message 需要输出的信息（无格式）
// 输出message，之后返回0
uint64 sys_print()
{
    char* message = "";
    arg_str(0, message, 256);
    printf(message);
    return 0;
}

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    uint64 new_heap_top;
    uint64 old_heap_top;
    uint64 res;
    pgtbl_t pgtbl = myproc()->pgtbl;

    arg_uint64(0, &new_heap_top);
    old_heap_top = myproc()->heap_top;

    if(new_heap_top == 0){
        // printf("堆未改变，堆查询结果：%x\n", old_heap_top);
        return old_heap_top;
    }
    else if(new_heap_top > old_heap_top){
        // 需要增大堆空间
        res = uvm_heap_grow(pgtbl, old_heap_top, new_heap_top - old_heap_top);
        // printf("增大的新堆：%x\n", res);
        return res;
    }
    else{
        // 需要缩小堆空间
        res = uvm_heap_ungrow(pgtbl, old_heap_top, old_heap_top - new_heap_top);
        // printf("缩小的新堆：%x\n", res);
        return res;
    }
}


// 实现进程分支
uint64 sys_fork()
{
    return proc_fork();
}

// 实现进程的等待
// uint64 进程的物理地址
// 成功返回子进程id，失败返回-1
uint64 sys_wait()
{
    uint64 p;
    arg_uint64(0, &p);
    return proc_wait(p);
}

// 实现进程的退出
// int 退出返回值
// 将进程以该值退出
uint64 sys_exit()
{
    int n;
    arg_int(0, &n);
    proc_exit(n);
    return 0;  // 这行不会到达
}

// 实现进程的睡眠
uint64 sys_sleep()
{
    int n;
    uint64 ticks0;

    arg_int(0, &n);
    spinlock_acquire(&timer_get()->lk);
    ticks0 = timer_get_ticks();
    while(timer_get_ticks() - ticks0 < n){
        if(proc_killed(myproc())){
            spinlock_release(&timer_get()->lk);
            return -1;
        }
        proc_sleep(&timer_get()->ticks, &timer_get()->lk);
    }
    spinlock_release(&timer_get()->lk);
    return 0;
}

// 实现进程的杀死
uint64 sys_kill()
{
    int pid;

    arg_int(0, &pid);
    return proc_kill(pid);
}

// 实现当前进程编号的获取
// 返回当前进程编号
uint64 sys_getpid()
{
    return myproc()->pid;
}

uint64 sys_setprior()
{
    int priority;

    arg_int( 0, &priority );

    return (uint64) proc_set_priority( priority );
}

uint64 sys_getprior()
{
    return (uint64) proc_get_priority();
}

uint64 sys_yield()
{
    proc_yield();
    return 0;
}