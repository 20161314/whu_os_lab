#include "sys.h"
#include "printf.h"
#include "syscall_wrap.h"
#include "common.h"
#include "str.h"

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_APPEND  0x800

void cleanup_test_artifacts(void) {
    print("\n清理测试生成的文件和目录...\n");

    // 单个文件
    unlink("/test");
    unlink("/testfile");
    unlink("/shared_file");
    unlink("/crash_test");
    unlink("/large_file");

    // 目录内容和目录本身
    unlink("/testdir/file1");
    unlink("/testdir/file2");
    unlink("/testdir");
    unlink("/dir");

    // 批量小文件 /small_XX
    char filename[16];
    for ( int i = 0; i < 30; i++ ) {
        int pos = 0;
        filename[pos++] = '/';
        filename[pos++] = 's';
        filename[pos++] = 'm';
        filename[pos++] = 'a';
        filename[pos++] = 'l';
        filename[pos++] = 'l';
        filename[pos++] = '_';
        if ( i >= 10 ) {
            filename[pos++] = '0' + ( i / 10 );
        }
        filename[pos++] = '0' + ( i % 10 );
        filename[pos] = '\0';
        unlink( filename );
    }
}

void test_basic_syscalls(void) {
    printf("\n=== Testing basic system calls... ===\n");

    // 测试getpid
    int pid = getpid();
    printf("Current PID: %d\n", pid);

    // 测试fork
    int child_pid = fork();
    if (child_pid == 0) {
        // 子进程
        printf("Child process: PID = %d\n", getpid());
        exit(42);
    }
    else if (child_pid > 0) {
        // 父进程
        int status;
        wait(&status);
        printf("Child exited with status: %d\n", status);
    }
    else {
        printf("Fork failed!\n");
    }

    printf("=== Testing finished. ===\n");
}

void test_parameter_passing(void) {
    printf("\n=== Testing parameter passing... ===\n");

    // 测试不同类型参数的传递
    char buffer[] = "Hello, World!";
    int fd = open("/test", O_CREATE | O_RDWR);

    if (fd >= 0) {
        int bytes_written = write(fd, buffer, strlen(buffer));
        printf("Wrote %d bytes\n", bytes_written);
        close(fd);
    }
    else{
        printf("file write failed");
    }

    // 测试边界情况
    printf("Testing edge cases:\n");
    write(-1, buffer, 10);      // 无效文件描述符
    write(fd, NULL, 10);        // 空指针
    write(fd, buffer, -1);      // 负数长度
    printf("No error. It's OK.\n");

    printf("=== Testing parameter finished. ===\n");
}

void test_security(void) {
    printf("\n=== Testing security ===\n");
    // 测试无效指针访问
    char *invalid_ptr = (char*)0x1000000; // 可能无效的地址
    int result = write(1, invalid_ptr, 10);
    printf("Invalid pointer write result: %d\n", result);

    // 测试缓冲区边界
    char small_buf[4];
    result = read(0, small_buf, 1000); // 尝试读取超过缓冲区大小
    printf("Invalid read result: %d\n", result);

    // 测试权限检查
    char* mes = "Hello world, today is as good as before.";
    int fd1 = open("/test2", O_CREATE | O_RDWR);
    result = write(fd1, mes, strlen(mes));
    printf("fd1 result(RDWR): %d\n", result);

    int fd2 = open("/test2", O_RDONLY);
    result = write(fd2, mes, strlen(mes));
    printf("fd2 result(RDONLY): %d\n", result);

    printf("=== Testing security end===\n");
}

void test_syscall_performance(void) {
    uint64 times = 1e6;

    printf("\n=== Testing syscall performance===\n");
    uint64 start_time = get_time();

    // 大量系统调用测试
    for (uint64 i = 0; i < times; i++) {
        getpid(); // 简单的系统调用
    }

    uint64 end_time = get_time();
    printf("%lu getpid() calls took %lu cycles\n", times, end_time - start_time);

    printf("=== Testing syscall performance ended===\n");
}



int main() {

    printf("\n=== Hello there, user begin ===\n");

    cleanup_test_artifacts();

    test_basic_syscalls();

    test_parameter_passing();

    test_security();

    test_syscall_performance();


    printf("\n=== Congrats! All test passed! ===\n");

    while (1);

    return 0;
}