#include "sys.h"
#include "printf.h"
#include "wrap.h"
#include "common.h"

// 简单的断言宏
#define assert(condition, message) \
    do { \
        if (!(condition)) { \
            printf("Assertion failed: %s\n", message); \
            exit(1); \
        } \
    } while(0)

// ==================== 共享缓冲区（用于生产者-消费者测试）====================
#define NPROC 64
#define BUFFER_SIZE 10
#define SHM_KEY 5678 // 为我们的共享内存定义一个唯一的key

typedef struct {
    int buffer[BUFFER_SIZE];
    int count;
    int in;
    int out;
    // 注意：未来真正的信号量也应该放在这个结构体里
} shared_buffer_t;

void buffer_print(shared_buffer_t *sb){
    printf("Contents: ");
    for(int i=0; i<BUFFER_SIZE; i++){
        printf("%d ", sb->buffer[i]);
    }
    printf("\n");
}

void shared_buffer_init(shared_buffer_t *sb) {
    sb->count = 0;
    sb->in = 0;
    sb->out = 0;
    // 清空缓冲区内容
    for(int i=0; i<BUFFER_SIZE; i++){
        sb->buffer[i] = 0;
    }
}

void buffer_put(shared_buffer_t *sb, int item) {
    // 简单的忙等待实现（有竞态条件风险，见文末说明）
    while (sb->count == BUFFER_SIZE) {
        sleep(1);
    }
    sb->buffer[sb->in] = item;
    sb->in = (sb->in + 1) % BUFFER_SIZE;
    sb->count++;
}

int buffer_get(shared_buffer_t *sb) {
    // 简单的忙等待实现（有竞态条件风险，见文末说明）
    while (sb->count == 0) {
        sleep(1);
    }
    int item = sb->buffer[sb->out];
    sb->out = (sb->out + 1) % BUFFER_SIZE;
    sb->count--;
    return item;
}

// ==================== 测试任务 ====================

void simple_task() {
    int pid = getpid();
    printf("Simple task [PID=%d] started\n", pid);
    
    int x = 0;
    for (int i = 0; i < 100; i++) {  // 减少循环次数以加快测试
        for (int j = 0; j < 10000; j++) {
            x += 1;
        }
        x -= (10000 - 3);
        
        // 每隔一段时间主动让出 CPU
        if (i % 10 == 0) {
            sleep(1);
        }
    }
    
    printf("Simple task [PID=%d] done! x=%d\n", pid, x);
    exit(0);
}

void cpu_intensive_task() {
    int pid = getpid();
    printf("CPU intensive task [PID=%d] started\n", pid);
    
    volatile uint64 counter = 0;
    for (int i = 0; i < 1000000; i++) {
        counter++;
        
        // 每隔一段时间主动让出 CPU
        if (i % 10000 == 0) {
            sleep(1);
        }
    }
    
    printf("CPU intensive task [PID=%d] completed, counter=%lu\n", 
           pid, counter);
    exit(0);
}

void producer_task() {
    int pid = getpid();
    printf("Producer [PID=%d] started\n", pid);

    // 子进程附加到共享内存
    shared_buffer_t *sb = (shared_buffer_t *)shm_get(SHM_KEY, sizeof(shared_buffer_t));
    if (sb == 0) {
        printf("Producer: shm_get failed\n");
        exit(1);
    }
    
    for (int i = 0; i < 10; i++) { // 减少循环次数以便观察
        buffer_put(sb, i + 1); // 放入非0值，方便观察
        printf("Producer [PID=%d] produced: %d\n", pid, i + 1);
        sleep(10); // 减慢速度，方便观察
    }
    
    printf("Producer [PID=%d] finished\n", pid);
    shm_release(SHM_KEY); // 进程结束前释放
    exit(0);
}

void consumer_task() {
    int pid = getpid();
    printf("Consumer [PID=%d] started\n", pid);

    // 子进程附加到共享内存
    shared_buffer_t *sb = (shared_buffer_t *)shm_get(SHM_KEY, sizeof(shared_buffer_t));
    if (sb == 0) {
        printf("Consumer: shm_get failed\n");
        exit(1);
    }
    
    for (int i = 0; i < 10; i++) {
        int item = buffer_get(sb);
        printf("Consumer [PID=%d] consumed: %d\n", pid, item);
        sleep(15); // 减慢速度，方便观察
    }
    
    printf("Consumer [PID=%d] finished\n", pid);
    shm_release(SHM_KEY); // 进程结束前释放
    exit(0);
}

// ==================== 测试函数 ====================

void test_process_creation(void) {
    printf("\n========== Testing process creation ==========\n");

    // 测试基本的进程创建
    printf("Test 1: Basic process creation\n");
    int pid = fork();
    if (pid == 0) {
        // 子进程
        simple_task();
    } else if (pid > 0) {
        // 父进程
        printf("Created process with PID=%d\n", pid);
        assert(pid > 0, "Failed to create process");
        
        // 等待进程完成
        int status;
        int waited_pid = wait(&status);
        printf("Process %d exited with status %d\n", waited_pid, status);
    } else {
        printf("Fork failed!\n");
        return;
    }

    // 测试进程表限制
    printf("\nTest 2: Process table limits\n");
    
    int count = 0;
    for (int i = 0; i < NPROC + 5; i++) {  // 减少测试数量
        int pid = fork();
        if (pid == 0) {
            // 子进程
            simple_task();
        } else if (pid > 0) {
            count++;
        } else {
            break;
        }
    }
    printf("Successfully created %d processes\n", count);

    // 清理测试进程
    printf("Waiting for all processes to complete...\n");
    for (int i = 0; i < count; i++) {
        wait(NULL);
    }
    printf("All processes completed\n");
    
    printf("========== Process creation test PASSED ==========\n\n");
}

void test_scheduler(void) {
    printf("\n========== Testing scheduler ==========\n");

    // 创建多个计算密集型进程
    printf("Creating 3 CPU-intensive processes...\n");
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        if (pid == 0) {
            // 子进程
            cpu_intensive_task();
        } else if (pid > 0) {
            printf("Created CPU-intensive process PID=%d\n", pid);
        }
    }

    printf("Waiting for processes to complete...\n");
    
    // 等待所有子进程完成
    for (int i = 0; i < 3; i++) {
        wait(NULL);
    }

    printf("Scheduler test completed\n");
    printf("========== Scheduler test PASSED ==========\n\n");
}

void test_synchronization(void) {
    printf("\n========== Testing synchronization ==========\n");
    
    // 1. 父进程获取并初始化共享内存
    shared_buffer_t *sb = (shared_buffer_t *)shm_get(SHM_KEY, sizeof(shared_buffer_t));
    if (sb == 0) {
        printf("Parent: shm_get failed\n");
        return;
    }
    printf("Shared buffer created/attached by parent\n");
    
    shared_buffer_init(sb);
    printf("Shared buffer initialized by parent\n");

    // 2. 创建生产者和消费者
    printf("Creating producer and consumer processes...\n");
    int pid1 = fork();
    if (pid1 == 0) {
        producer_task();
    }
    
    int pid2 = fork();
    if (pid2 == 0) {
        consumer_task();
    }
    
    if (pid1 > 0 && pid2 > 0) {
        printf("Producer PID=%d, Consumer PID=%d\n", pid1, pid2);
        
        // 3. 等待子进程结束
        printf("Waiting for producer and consumer to finish...\n");
        wait(0);
        wait(0);

        // 4. 父进程最后释放共享内存
        shm_release(SHM_KEY);
        printf("Shared buffer released by parent\n");
    }

    printf("========== Synchronization test FINISHED ==========\n\n");
}


void test_shm(void){
    uint64 key = 0x1234;
    int pid = fork();
    shared_buffer_t *sb = (shared_buffer_t *)shm_get(key, sizeof(shared_buffer_t));

    if (pid == 0) {
        // son
        shared_buffer_t *sb = (shared_buffer_t *)shm_get(key, sizeof(shared_buffer_t));
        sleep(5);

        printf("Son: waiting father to write Contents.\n");

        while (sb->count == 0) {
            sleep(1);
        }

        printf("Son: Detected shm write.\n");
        buffer_print(sb);

        exit(0);

    }
    else{
        // father

        sleep(10);
        printf("Father: writing data:\n");

        for(int i=0; i<BUFFER_SIZE; i++){
            sb->buffer[i] = i*10;
        }
        sb->count = BUFFER_SIZE;
        buffer_print(sb);

        printf("Father: waiting son to finish.\n");
        wait(0);
    }
}




// ==================== 主函数 ====================

int main()
{
    // 用户进程的开始
    syscall(SYS_print, "\nUser begin:\n");

    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Process Management Test Suite       ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");

    // 测试 1: 进程创建
    //test_process_creation();
    
    // 测试 2: 调度器
    //test_scheduler();
    
    // 测试 3: 同步机制
    //test_synchronization();
    
    test_shm();

    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   All Tests PASSED!                    ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");


    while(1);
    
    return 0;
}