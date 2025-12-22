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

void priority_scheduler_test( void ) {
    printf( "=== 优先级时间片轮转测试 ===\n\n" );

    int base_priority = getpriority();
    printf("父进程初始优先级: %d\n", base_priority);

    const int child_priority[] = { 7, 7, 7, 11, 5 };
    const char child_tag[] = { 'A', 'B', 'C', 'D', 'E' };
    const int child_count = sizeof( child_priority ) / sizeof( child_priority[0] );
    const int slice_rounds = 4;

    printf( "说明: 子进程 A/B/C 具有相同优先级，用于观察时间片轮转效果；D/E 分别展示高/低优先级穿插。\n\n" );

    for ( int i = 0; i < child_count; i++ ) {
        int desired = child_priority[i];
        char tag_buf[2] = { child_tag[i], '\0' };

        printf("准备创建子进程 %s, 目标优先级 = %d\n", tag_buf, desired);

        // 让新建的子进程继承目标优先级
        setpriority( desired );
        int pid = fork();

        if ( pid < 0 ) {
            print( "fork 失败，终止优先级测试\n" );
            setpriority( base_priority );
            return;
        }

        if ( pid == 0 ) {
            printf("子进程 %s 启动，优先级 = %d\n", tag_buf, desired);

            for ( int round = 0; round < slice_rounds; round++ ) {
                printf("  [%s | 优先级=%d] 第 %d 轮 -> 运行\n"
                        , tag_buf, desired, round);

                for ( volatile int spin = 0; spin < 150000; spin++ ) {
                    // 忙等模拟工作负载
                }

                printf("  [%s] 让出 CPU\n", tag_buf);

                yield();
            }

            printf("子进程 %s 完成所有轮次\n", tag_buf);

            exit( 0 );
        }

        // 父进程恢复自身优先级并记录信息
        setpriority( base_priority );
        printf("父进程创建了 PID=%d 的子进程 %s\n", pid, tag_buf);
    }

    printf( "\n父进程等待所有子进程轮流运行...\n" );
    for ( int i = 0; i < child_count; i++ ) {
        int status = 0;
        int waited = wait( &status );

        printf("回收子进程 PID=%d, 状态=%d\n", waited, status);
    }

    printf( "父进程优先级仍为: %d.\n", getpriority());
    printf( "=== 优先级时间片轮转测试结束 ===\n\n" );
}

int main() {

    printf( "=== main() ===\n\n" );

    // cleanup_test_artifacts();

    priority_scheduler_test();

    // 由于当前用户 main 是寄生在 init_proc 中的，所以不可以退出
    while ( 1 );

    return 0;
}