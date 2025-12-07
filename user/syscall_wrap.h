#include "sys.h"
#include "common.h"

void sys_test(void)
{
    syscall(SYS_test);
}

int sys_print(const char * str)
{
    return syscall(SYS_print, str);
}

int sys_brk(int size)
{
    return syscall(SYS_open, size);
}

int sys_open(char * dir, int state)
{
    return syscall(SYS_open, dir, state);
}

int sys_close(int fd)
{
    return syscall(SYS_close, fd);
}

int sys_fork()
{
    return syscall(SYS_fork);
}

int sys_wait(int* addr)
{
    return syscall(SYS_wait, (uint64)addr);
}

int sys_exit(int n)
{
    return syscall(SYS_exit, n);
}

int sys_sleep(int n)
{
    return syscall(SYS_sleep, n);
}

int sys_kill(int n)
{
    return syscall(SYS_kill, n);
}

int sys_getpid(void)
{
    return syscall(SYS_getpid);
}

int sys_read(int fd, char* buf, int sz)
{
    return syscall(SYS_read, fd, buf, sz);
}

int sys_write(int fd, const char* msg, int len)
{
    return syscall(SYS_write, fd, msg, len);
}

int sys_mkdir(char* dir)
{
    return syscall(SYS_mkdir, dir);
}

int sys_link(char *f1, char* f2)
{
    return syscall(SYS_link, f1, f2);
}

int sys_unlink(char * fd)
{
    return syscall(SYS_unlink, fd);
}

int sys_fstat(int fs, char * addr)
{
    return syscall(SYS_fstat, fs, addr);
}

int sys_dup(int fs)
{
    return syscall(SYS_dup, fs);
}

int sys_setpriority(int prior)
{
    return syscall(SYS_setprior, prior);
}

int sys_getpriority(void)
{
    return syscall(SYS_getprior);
}

int sys_yield(void)
{
    return syscall(SYS_yield);
}