#include "sys.h"
#include "syscall_wrap.h"

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_APPEND  0x800

void print_int( int n ) {
    char buf[20];
    int i = 0;
    if ( n == 0 ) {
        sys_print( "0" );
        return;
    }
    if ( n < 0 ) {
        sys_print( "-" );
        n = -n;
    }
    while ( n > 0 ) {
        buf[i++] = '0' + n % 10;
        n /= 10;
    }
    while ( i > 0 ) {
        char c[2] = { buf[--i], '\0' };
        sys_print( c );
    }
}

static void cleanup_test_artifacts(void) {
    sys_print("\n清理测试生成的文件和目录...\n");

    // 单个文件
    sys_unlink("/test");
    sys_unlink("/testfile");
    sys_unlink("/shared_file");
    sys_unlink("/crash_test");
    sys_unlink("/large_file");

    // 目录内容和目录本身
    sys_unlink("/testdir/file1");
    sys_unlink("/testdir/file2");
    sys_unlink("/testdir");
    sys_unlink("/dir");

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
        sys_unlink( filename );
    }
}

static void priority_scheduler_test( void ) {
    sys_print( "=== 优先级时间片轮转测试 ===\n\n" );

    int base_priority = sys_getpriority();
    sys_print( "父进程初始优先级: " );
    print_int( base_priority );
    sys_print( "\n" );

    const int child_priority[] = { 7, 7, 7, 11, 5 };
    const char child_tag[] = { 'A', 'B', 'C', 'D', 'E' };
    const int child_count = sizeof( child_priority ) / sizeof( child_priority[0] );
    const int slice_rounds = 4;

    sys_print( "说明: 子进程 A/B/C 具有相同优先级，用于观察时间片轮转效果；D/E 分别展示高/低优先级穿插。\n\n" );

    for ( int i = 0; i < child_count; i++ ) {
        int desired = child_priority[i];
        char tag_buf[2] = { child_tag[i], '\0' };

        sys_print( "准备创建子进程 " );
        sys_print( tag_buf );
        sys_print( ", 目标优先级=" );
        print_int( desired );
        sys_print( "\n" );

        // 让新建的子进程继承目标优先级
        sys_setpriority( desired );
        int pid = sys_fork();

        if ( pid < 0 ) {
            sys_print( "fork 失败，终止优先级测试\n" );
            sys_setpriority( base_priority );
            return;
        }

        if ( pid == 0 ) {
            sys_print( "子进程 " );
            sys_print( tag_buf );
            sys_print( " 启动, 优先级=" );
            print_int( desired );
            sys_print( "\n" );

            for ( int round = 0; round < slice_rounds; round++ ) {
                sys_print( "  [" );
                sys_print( tag_buf );
                sys_print( " | 优先级=" );
                print_int( desired );
                sys_print( "] 第 " );
                print_int( round );
                sys_print( " 轮 -> 运行\n" );

                for ( volatile int spin = 0; spin < 150000; spin++ ) {
                    // 忙等模拟工作负载
                }

                sys_print( "  [" );
                sys_print( tag_buf );
                sys_print( "] 让出 CPU\n" );
                sys_yield();
            }

            sys_print( "子进程 " );
            sys_print( tag_buf );
            sys_print( " 完成所有轮次\n" );
            sys_exit( 0 );
        }

        // 父进程恢复自身优先级并记录信息
        sys_setpriority( base_priority );
        sys_print( "父进程创建了 PID=" );
        print_int( pid );
        sys_print( " 的子进程 " );
        sys_print( tag_buf );
        sys_print( "\n" );
    }

    sys_print( "\n父进程等待所有子进程轮流运行...\n" );
    for ( int i = 0; i < child_count; i++ ) {
        int status = 0;
        int waited = sys_wait( &status );
        sys_print( "回收子进程 PID=" );
        print_int( waited );
        sys_print( ", 状态=" );
        print_int( status );
        sys_print( "\n" );
    }

    sys_print( "父进程优先级仍为: " );
    print_int( sys_getpriority() );
    sys_print( "\n" );
    sys_print( "=== 优先级时间片轮转测试结束 ===\n\n" );
}

int main() {

    sys_print( "=== main() ===\n\n" );

    cleanup_test_artifacts();

    priority_scheduler_test();

    // 由于当前用户 main 是寄生在 init_proc 中的，所以不可以退出
    while ( 1 );

    return 0;
}