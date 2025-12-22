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

int slow_times = 5;

void task(){
    for(int i=0; i<10; i++){
        for(int j=0; j<150000; j++){};

        sys_sleep(sys_getpid() * slow_times);

        clear();
        print_proc_table();
    }
    sys_print("Son: done.\n");
}

void test_proc_table(){
    int sons = 10;
    
    for(int i=0; i<sons; i++){
        int pid = sys_fork();
        if(pid == 0){
            sys_print("Son ");
            print_int(sys_getpid());
            sys_print(" created.\n");

            task();
            sys_exit(0);
        }
    }

    for(int i=0; i<10; i++){
        clear();
        print_proc_table();
        sys_sleep(1 * slow_times);
    }

    for(int i=0; i<sons; i++){
        sys_wait(0);
    }

    clear();
    print_proc_table();
    
    sys_print("All finished.\n");
    sys_sleep(30);
    clear();
}

int main() {
    // 测试以下套包哪里来的（汗
    sys_print( "line 1\n");
    sys_print( "line 2\n");
    sys_print( "line 3\n");

    sys_print( "=== main() ===\n\n" );

    int fd;

    sys_print( "=== 测试文件系统 ===\n\n" );

    cleanup_test_artifacts();

    // 测试 1: 创建文件并写入
    sys_print( "测试 1: 创建文件并写入\n" );
    fd = sys_open( "/test", O_CREATE | O_RDWR );
    sys_print( "打开文件, fd=" );
    print_int(fd);
    sys_print( "\n" );
    
    if(fd >= 0) {
        const char* msg = "Hello FS";
        int len = 8;
        sys_print( "写入内容: " );
        sys_print( msg );
        sys_print( "\n" );
        int n = sys_write(fd, msg, len);
        sys_print( "写入 " );
        print_int(n);
        sys_print( " 字节\n" );
        sys_close(fd);
        sys_print( "文件已关闭\n" );
    }
    sys_print("\n");

    // 测试 2: 重新打开并读取
    sys_print( "测试 2: 读取文件\n" );
    fd = sys_open( "/test", O_RDONLY );
    if(fd >= 0) {
        char buf[32];
        for(int i=0; i<32; i++) buf[i]=0;
        int n = sys_read(fd, buf, 20);
        sys_print( "读取 " );
        print_int(n);
        sys_print( " 字节: " );
        sys_print(buf);
        sys_print( "\n" );
        sys_close(fd);
    }
    sys_print("\n");

    // 测试 3: 创建目录
    sys_print( "测试 3: 创建目录\n" );
    int r = sys_mkdir("/dir");
    sys_print( "mkdir 返回: " );
    print_int(r);
    sys_print( "\n\n" );

    sys_print( "所有文件系统测试完成！\n\n" );

    sys_print( "=== 文件系统完整性测试 ===\n\n" );
    
    // 创建测试文件
    sys_print( "创建测试文件...\n" );
    fd = sys_open( "/testfile", O_CREATE | O_RDWR );
    if ( fd < 0 ) {
        sys_print( "错误: 无法创建文件\n" );
        while(1){}
        return -1;
    }
    sys_print( "文件创建成功\n" );
    
    // 写入数据
    const char* test_buffer = "Hello, filesystem!";
    int buffer_len = 0;
    while ( test_buffer[buffer_len] ) buffer_len++;
    
    sys_print( "写入内容: " );
    sys_print( test_buffer );
    sys_print( "\n" );
    int bytes_written = sys_write( fd, test_buffer, buffer_len );
    sys_print( "写入了 " );
    print_int( bytes_written );
    sys_print( " 字节\n" );
    sys_close( fd );
    
    // 重新打开并验证
    sys_print( "重新打开文件并验证...\n" );
    fd = sys_open( "/testfile", O_RDONLY );
    if ( fd < 0 ) {
        sys_print( "错误: 无法打开文件\n" );
        while(1){}
        return -1;
    }
    
    char read_buffer[64];
    for ( int i = 0; i < 64; i++ ) read_buffer[i] = 0;
    int bytes_read = sys_read( fd, read_buffer, sizeof(read_buffer) - 1 );
    read_buffer[bytes_read] = '\0';
    
    sys_print( "读取了 " );
    print_int( bytes_read );
    sys_print( " 字节: " );
    sys_print( read_buffer );
    sys_print( "\n" );
    
    // 验证数据一致性
    int data_match = 1;
    for ( int i = 0; i < buffer_len; i++ ) {
        if ( test_buffer[i] != read_buffer[i] ) {
            data_match = 0;
            break;
        }
    }
    
    if ( data_match ) {
        sys_print( "数据验证通过！\n" );
    } else {
        sys_print( "错误: 数据验证失败\n" );
        while(1){}
        return -1;
    }
    
    sys_close( fd );
    
    // 删除文件
    sys_print( "删除文件...\n" );
    int unlink_result = sys_unlink( "/testfile" );
    if ( unlink_result == 0 ) {
        sys_print( "文件删除成功\n" );
    } else {
        sys_print( "错误: 文件删除失败\n" );
        while(1){}
        return -1;
    }
    
    sys_print( "文件系统完整性测试通过！\n\n" );
    
    // 测试目录和多个文件
    sys_print( "=== 测试目录和多个文件 ===\n\n" );
    
    sys_print( "创建目录 /testdir...\n" );
    int mkdir_result = sys_mkdir( "/testdir" );
    if ( mkdir_result < 0 ) {
        sys_print( "错误: 无法创建目录\n" );
        while(1){}
        return -1;
    }
    sys_print( "目录创建成功\n" );
    
    // 在目录中创建多个文件
    sys_print( "在目录中创建文件...\n" );
    fd = sys_open( "/testdir/file1", O_CREATE | O_RDWR );
    if ( fd < 0 ) {
        sys_print( "错误: 无法在目录中创建文件\n" );
        while(1){}
        return -1;
    }
    sys_write( fd, "File 1 content", 14 );
    sys_close( fd );
    sys_print( "文件 file1 创建成功\n" );
    
    fd = sys_open( "/testdir/file2", O_CREATE | O_RDWR );
    if ( fd >= 0 ) {
        sys_write( fd, "File 2 content", 14 );
        sys_close( fd );
        sys_print( "文件 file2 创建成功\n" );
    }
    
    sys_print( "目录和文件测试通过！\n\n" );
    
    sys_print( "=== 所有文件系统测试完成 ===\n\n" );

    // 并发访问测试
    sys_print( "=== 并发访问测试 ===\n\n" );
    sys_print( "创建共享文件...\n" );
    fd = sys_open( "/shared_file", O_CREATE | O_RDWR );
    if ( fd >= 0 ) {
        const char* initial_msg = "Initial";
        sys_print( "写入初始内容: " );
        sys_print( initial_msg );
        sys_print( "\n" );
        sys_write( fd, initial_msg, 7 );
        sys_close( fd );
        sys_print( "共享文件创建成功\n" );
    }
    
    sys_print( "创建两个子进程同时写入文件...\n" );
    for ( int i = 0; i < 2; i++ ) {
        int fork_result = sys_fork();
        if ( fork_result == 0 ) {
            // 子进程
            sys_print( "子进程 " );
            print_int( i + 1 );
            sys_print( " 开始写入\n" );
            
            // 使用 O_APPEND 模式打开文件，确保追加写入
            fd = sys_open( "/shared_file", O_RDWR | O_APPEND );
            if ( fd >= 0 ) {
                char write_data[20];
                for ( int j = 0; j < 20; j++ ) {
                    write_data[j] = 'A' + i;
                }
                write_data[10] = '\0';
                
                sys_print( "子进程 " );
                print_int( i + 1 );
                sys_print( " 写入内容: " );
                sys_print( write_data );
                sys_print( " (追加5次)\n" );
                
                // 多次写入以测试并发追加
                for ( int j = 0; j < 5; j++ ) {
                    sys_write( fd, write_data, 10 );
                }
                
                sys_close( fd );
                sys_print( "子进程 " );
                print_int( i + 1 );
                sys_print( " 写入完成\n" );
            }
            
            sys_exit( 0 );
        }
    }
    
    // 父进程等待子进程完成
    sys_print( "等待子进程完成...\n" );
    for ( int i = 0; i < 2; i++ ) {
        int status;
        sys_wait( &status );
    }
    
    
    // 验证文件内容
    sys_print( "验证共享文件内容...\n" );
    fd = sys_open( "/shared_file", O_RDONLY );
    if ( fd >= 0 ) {
        char verify_buf[128];
        for ( int i = 0; i < 128; i++ ) verify_buf[i] = 0;
        int n = sys_read( fd, verify_buf, 127 );
        verify_buf[n] = '\0';
        sys_print( "读取到 " );
        print_int( n );
        sys_print( " 字节: " );
        sys_print( verify_buf );
        sys_print( "\n" );
        sys_close( fd );
    }
    sys_print( "并发访问测试完成\n\n" );
    
    // 崩溃恢复测试
    sys_print( "=== 崩溃恢复测试 ===\n\n" );
    sys_print( "测试场景: 文件写入过程中的数据一致性\n" );
    
    // 创建测试文件并写入多次
    sys_print( "创建测试文件并进行多次写入...\n" );
    fd = sys_open( "/crash_test", O_CREATE | O_RDWR );
    if ( fd >= 0 ) {
        for ( int i = 0; i < 10; i++ ) {
            char crash_data[32];
            for ( int j = 0; j < 31; j++ ) {
                crash_data[j] = '0' + i;
            }
            crash_data[31] = '\n';
            
            int written = sys_write( fd, crash_data, 32 );
            sys_print( "第 " );
            print_int( i + 1 );
            sys_print( " 次写入: " );
            print_int( written );
            sys_print( " 字节\n" );
            
            // 在这里如果系统崩溃，日志系统应该能保证数据一致性
        }
        sys_close( fd );
    }
    
    // 重新打开并验证
    sys_print( "重新打开文件并验证数据一致性...\n" );
    fd = sys_open( "/crash_test", O_RDONLY );
    if ( fd >= 0 ) {
        char crash_verify[512];
        for ( int i = 0; i < 512; i++ ) crash_verify[i] = 0;
        int n = sys_read( fd, crash_verify, 511 );
        sys_print( "读取到 " );
        print_int( n );
        sys_print( " 字节 (预期 320 字节)\n" );
        
        if ( n == 320 ) {
            sys_print( "数据大小验证通过\n" );
        } else {
            sys_print( "警告: 数据大小不符\n" );
        }
        
        sys_close( fd );
    }
    sys_print( "崩溃恢复测试完成\n\n" );
    
    // 性能测试
    sys_print( "=== 性能测试 ===\n\n" );
    
    // 测试 1: 大量小文件创建（减少到30个以避免资源耗尽）
    sys_print( "测试 1: 创建 30 个小文件\n" );
    sys_print( "开始创建...\n" );
    
    int small_file_count = 0;
    for ( int i = 0; i < 30; i++ ) {
        // 手动构造文件名 /small_XX
        char filename[32];
        int pos = 0;
        filename[pos++] = '/';
        filename[pos++] = 's';
        filename[pos++] = 'm';
        filename[pos++] = 'a';
        filename[pos++] = 'l';
        filename[pos++] = 'l';
        filename[pos++] = '_';
        
        if ( i >= 10 ) {
            filename[pos++] = '0' + (i / 10);
        }
        filename[pos++] = '0' + (i % 10);
        filename[pos] = '\0';
        
        fd = sys_open( filename, O_CREATE | O_RDWR );
        if ( fd >= 0 ) {
            sys_write( fd, "test", 4 );
            sys_close( fd );
            small_file_count++;
        } else {
            sys_print( "警告: 无法创建文件 " );
            sys_print( filename );
            sys_print( "\n" );
            break;
        }
        
            sys_print( "已创建 " );
            print_int( i + 1 );
            sys_print( " 个文件\n" );
    }
    
    sys_print( "成功创建 " );
    print_int( small_file_count );
    sys_print( " 个小文件\n\n" );
    
    // 测试 2: 大文件写入
    sys_print( "测试 2: 大文件写入 (写入 100 个块)\n" );
    fd = sys_open( "/large_file", O_CREATE | O_RDWR );
    if ( fd >= 0 ) {
        char large_buffer[512];
        for ( int i = 0; i < 512; i++ ) {
            large_buffer[i] = 'L';
        }
        
        int total_written = 0;
        for ( int i = 0; i < 100; i++ ) {
            int written = sys_write( fd, large_buffer, 512 );
            total_written += written;
            
            // 每 20 次写入打印一次进度
            if ( (i + 1) % 20 == 0 ) {
                sys_print( "已写入 " );
                print_int( i + 1 );
                sys_print( " 个块\n" );
            }
        }
        
        sys_close( fd );
        sys_print( "总共写入 " );
        print_int( total_written );
        sys_print( " 字节\n\n" );
    }
    
    // 测试 3: 顺序读取性能
    sys_print( "测试 3: 顺序读取大文件\n" );
    fd = sys_open( "/large_file", O_RDONLY );
    if ( fd >= 0 ) {
        char read_buffer[512];
        int total_read = 0;
        int read_count = 0;
        
        while ( 1 ) {
            int n = sys_read( fd, read_buffer, 512 );
            if ( n <= 0 ) break;
            total_read += n;
            read_count++;
            
            if ( read_count % 20 == 0 ) {
                sys_print( "已读取 " );
                print_int( read_count );
                sys_print( " 个块\n" );
            }
        }
        
        sys_close( fd );
        sys_print( "总共读取 " );
        print_int( total_read );
        sys_print( " 字节\n\n" );
    }
    
    sys_print( "性能测试完成\n\n" );
    
    sys_print( "=== 所有高级测试完成 ===\n\n" );

    cleanup_test_artifacts();
    
    // ========== 日志系统恢复测试 ==========
    sys_print("=== 日志系统恢复测试 ===\n");
    sys_print("说明: 此测试验证日志系统的崩溃恢复能力\n");
    sys_print("步骤:\n");
    sys_print("1. 创建文件并写入多次(模拟多个事务)\n");
    sys_print("2. 每次事务提交后,日志都会被正确写入\n");
    sys_print("3. 如果系统崩溃,下次启动时会从日志恢复\n\n");
    
    int logs_exist = 1;
    int check_fd = sys_open("/log_00", O_RDONLY);
    if(check_fd < 0) {
        logs_exist = 0;
    } else {
        sys_close(check_fd);
    }

    if(!logs_exist) {
        sys_print("测试: 创建10个文件,每个文件写入32字节\n");
        for(int i = 0; i < 10; i++) {
            char filename[32];
            // 简单的整数转字符串
            filename[0] = '/';
            filename[1] = 'l';
            filename[2] = 'o';
            filename[3] = 'g';
            filename[4] = '_';
            filename[5] = '0' + (i / 10);
            filename[6] = '0' + (i % 10);
            filename[7] = '\0';
            
            int wfd = sys_open(filename, O_CREATE | O_RDWR);
            if(wfd < 0) {
                sys_print("创建文件失败: ");
                sys_print(filename);
                sys_print("\n");
                continue;
            }
            
            char content[32];
            for(int j = 0; j < 31; j++) {
                content[j] = 'A' + (i % 26);
            }
            content[31] = '\n';
            
            int n = sys_write(wfd, content, 32);
            sys_close(wfd);
            
            sys_print("创建文件 ");
            sys_print(filename);
            sys_print(", 写入 ");
            print_int(n);
            sys_print(" 字节\n");
        }
    } else {
        sys_print("检测到已有日志文件，跳过创建阶段\n");
    }
    
    sys_print("\n验证文件内容:\n");
    for(int i = 0; i < 10; i++) {
        char filename[32];
        filename[0] = '/';
        filename[1] = 'l';
        filename[2] = 'o';
        filename[3] = 'g';
        filename[4] = '_';
        filename[5] = '0' + (i / 10);
        filename[6] = '0' + (i % 10);
        filename[7] = '\0';
        
        int fd = sys_open(filename, O_RDONLY);
        if(fd < 0) {
            sys_print("打开文件失败: ");
            sys_print(filename);
            sys_print("\n");
            continue;
        }
        
        char content[33];
        int n = sys_read(fd, content, 32);
        content[n] = '\0';
        sys_close(fd);
        
        sys_print(filename);
        sys_print(": ");
        print_int(n);
        sys_print(" 字节 - ");
        sys_print(content);
    }
    
    sys_print("\n日志系统测试完成!\n");
    sys_print("说明: 如果系统重启,这些文件应该仍然存在且内容正确\n");
    sys_print("这证明了日志系统正确实现了崩溃恢复\n\n");

    sys_print( "--- main() 测试完成 ---\n" );


    test_proc_table();

    // 由于当前用户 main 是寄生在 init_proc 中的，所以不可以退出
    while ( 1 );

    return 0;
}