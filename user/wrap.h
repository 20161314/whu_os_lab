#include "common.h"

int getpid(void) {
    return syscall(SYS_getpid);
}

int fork(void) {
    return syscall(SYS_fork);
}

int wait(void* p) {
    return syscall(SYS_wait, p);
}

int exit(int x) {
    return syscall(SYS_exit, x);
}

void sleep(int ticks){
    syscall(SYS_sleep, ticks);
}

int64 shm_get(int key, int size){
    return syscall(SYS_mmap, key, size);
}

int shm_release(int key){
    return syscall(SYS_munmap, key);
}