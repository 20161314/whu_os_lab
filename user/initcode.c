#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - 34 * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 8096 * PGSIZE) 

char *str1, *str2;

int main()
{
    // 用户进程的开始
    syscall(SYS_print, "\nUser begin\n");

    // 空白系统调用测试
    syscall(SYS_print, "\nTesting blank system call:\n");
    syscall(SYS_test);

    // 进程号调用测试（基本）
    syscall(SYS_print, "\nTesting getting pid:\n");
    int id = syscall(SYS_getpid);
    char* str = "pid: _\n";
    str[5] = '0' + id;
    syscall(SYS_print, str);

    // 输出系统调用测试（输出所有非控制ascii字符，16个1行）
    syscall(SYS_print, "\nTesting printing ascii:\n");
    for(int i=0x20; i<0x7F; i++){
        char* str = "_ ";
        str[0] = i;
        syscall(SYS_print, str);
        if((i - 0x20) % 16 == 15){
            syscall(SYS_print, "\n");
        }
    }
    syscall(SYS_print, "\n");

    // 测试HEAP区域
    syscall(SYS_print, "\nTesting heap:\n");
    long long top = syscall(SYS_brk, 0);
    str2 = (char*)top;
    syscall(SYS_brk, top + PGSIZE);
    str2[0] = 'H';
    str2[1] = 'E';
    str2[2] = 'A';
    str2[3] = 'P';
    str2[4] = '\n';
    str2[5] = '\0';
    syscall(SYS_print, str2);

    // 测试进程的fork, wait, exit
    syscall(SYS_print, "\nTesting forking and waiting:\n");
    int pid = syscall(SYS_fork);
    if(pid == 0) { // 子进程
        for(int i = 0; i < 100000000; i++);
        syscall(SYS_print, "child: hello\n");
        syscall(SYS_exit, 1);
        syscall(SYS_print, "child: never back\n");
    } else {       // 父进程
        int exit_state;
        int sonid = syscall(SYS_wait, &exit_state);
        if(sonid == pid && exit_state == 1)
            syscall(SYS_print, "parent: hello\n");
        else
            syscall(SYS_print, "parent: error\n");
    }

    // 测试进程的杀死
    syscall(SYS_print, "\nTesting Killing:\n");
    int sons = 5;
    int pids[sons];
    for(int i=0; i<sons; i++){
        int pid = syscall(SYS_fork);
        if(pid == 0){
            syscall(SYS_print, "child: hello\n");
            while(1) {}
        }
        else{
            pids[i] = pid;
        }
    }
    for(int i = 0; i < 100000000; i++);
    for(int i=0; i<sons; i++){
        int res = syscall(SYS_kill, pids[i]);
        if(res == 0){
            syscall(SYS_print, "parent: kill success\n");
        }
        else{
            syscall(SYS_print, "parent: kill failed\n");
        }
    }

    // 测试进程的sleep
    syscall(SYS_print, "\nTesting Sleeping:\n");
    for(int i=0; i<10; i++){
        syscall(SYS_sleep, 10);
        syscall(SYS_print, "z");
    }
    syscall(SYS_print, "\nSleeped for 10s succeed.\n");

    // 快速系统调用压力测试
    syscall(SYS_print, "\nTesting Fast syscall:\n");
    int times = 100000;
    for(int i=0; i<times; i++){
        // syscall(SYS_test);
        syscall(SYS_sleep, 0);
        syscall(SYS_getpid);
        
        //char* c = (i % 10000 == 0) ? "#" : "";
        //syscall(SYS_print, c);
        syscall(SYS_print, ".");
    }
    syscall(SYS_print, "\nFast syscall test succeed.\n");

    while(1);
    
    return 0;
}