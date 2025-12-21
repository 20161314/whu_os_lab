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