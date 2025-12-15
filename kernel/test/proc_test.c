#include "proc/proc.h"
#include "dev/console.h"
#include "dev/timer.h"
#include "lib/lock.h"
#include "proc/cpu.h"
#include "test/proc_test.h"

extern proc_t proc[NPROC];

// ==================== 共享缓冲区（用于生产者-消费者测试）====================
#define BUFFER_SIZE 10

typedef struct {
    int buffer[BUFFER_SIZE];
    int count;
    int in;
    int out;
    spinlock_t lock;
} shared_buffer_t;

static shared_buffer_t shared_buffer;

void shared_buffer_init() {
    shared_buffer.count = 0;
    shared_buffer.in = 0;
    shared_buffer.out = 0;
    spinlock_init(&shared_buffer.lock, "shared_buffer");
}

void buffer_put(int item) {
    spinlock_acquire(&shared_buffer.lock);
    while (shared_buffer.count == BUFFER_SIZE) {
        // 缓冲区满，等待
        proc_sleep(&shared_buffer.count, &shared_buffer.lock);
    }
    shared_buffer.buffer[shared_buffer.in] = item;
    shared_buffer.in = (shared_buffer.in + 1) % BUFFER_SIZE;
    shared_buffer.count++;
    proc_wakeup(&shared_buffer.count);
    spinlock_release(&shared_buffer.lock);
}

int buffer_get() {
    spinlock_acquire(&shared_buffer.lock);
    while (shared_buffer.count == 0) {
        // 缓冲区空，等待
        proc_sleep(&shared_buffer.count, &shared_buffer.lock);
    }
    int item = shared_buffer.buffer[shared_buffer.out];
    shared_buffer.out = (shared_buffer.out + 1) % BUFFER_SIZE;
    shared_buffer.count--;
    proc_wakeup(&shared_buffer.count);
    spinlock_release(&shared_buffer.lock);
    return item;
}

// ==================== 辅助函数 ====================

// sleep 函数：让进程休眠 n 个时钟周期
void sleep(int n) {
    if (n <= 0) return;
    
    timer_t *timer = timer_get();
    spinlock_acquire(&timer->lk);
    
    int ticks0 = timer_get_ticks();
    while (timer_get_ticks() - ticks0 < n) {
        if (proc_killed(myproc())) {
            spinlock_release(&timer->lk);
            proc_exit(-1);
        }
        proc_sleep(&timer->ticks, &timer->lk);
    }
    
    spinlock_release(&timer->lk);
}

// wait_process：等待任意子进程退出
int wait_process(int *status) {
    proc_t *p = myproc();
    
    spinlock_acquire(&p->lk);
    
    for (;;) {
        // 查找已退出的子进程
        int havekids = 0;
        for (int i = 0; i < NPROC; i++) {
            proc_t *child = &proc[i];
            if (child->parent == p) {
                havekids = 1;
                
                spinlock_acquire(&child->lk);
                if (child->state == ZOMBIE) {
                    // 找到僵尸进程，回收资源
                    int pid = child->pid;
                    if (status != NULL) {
                        *status = child->xstate;
                    }
                    
                    // 释放子进程资源
                    proc_free(child);
                    spinlock_release(&child->lk);
                    spinlock_release(&p->lk);
                    return pid;
                }
                spinlock_release(&child->lk);
            }
        }
        
        // 没有子进程
        if (!havekids || proc_killed(p)) {
            spinlock_release(&p->lk);
            return -1;
        }
        
        // 等待子进程退出
        proc_sleep(p, &p->lk);
    }
}

// ==================== 测试任务 ====================

void simple_task() {
    printf("Simple task [PID=%d] started\n", myproc()->pid);
    
    int x = 0;
    for (int i = 0; i < 100; i++) {  // 减少循环次数以加快测试
        for (int j = 0; j < 10000; j++) {
            x += 1;
        }
        x -= (10000 - 3);
        
        // 每隔一段时间主动让出 CPU
        if (i % 10 == 0) {
            proc_yield();
        }
    }
    
    printf("Simple task [PID=%d] done! x=%d\n", myproc()->pid, x);
    proc_exit(0);
}

void cpu_intensive_task() {
    printf("CPU intensive task [PID=%d] started\n", myproc()->pid);
    
    volatile uint64 counter = 0;
    for (int i = 0; i < 1000000; i++) {
        counter++;
        
        // 每隔一段时间主动让出 CPU
        if (i % 10000 == 0) {
            proc_yield();
        }
    }
    
    printf("CPU intensive task [PID=%d] completed, counter=%lu\n", 
           myproc()->pid, counter);
    proc_exit(0);
}

void producer_task() {
    printf("Producer [PID=%d] started\n", myproc()->pid);
    
    for (int i = 0; i < 20; i++) {
        buffer_put(i);
        printf("Producer [PID=%d] produced: %d\n", myproc()->pid, i);
        
        // 模拟生产延迟
        for (volatile int j = 0; j < 100000; j++);
    }
    
    printf("Producer [PID=%d] finished\n", myproc()->pid);
    proc_exit(0);
}

void consumer_task() {
    printf("Consumer [PID=%d] started\n", myproc()->pid);
    
    for (int i = 0; i < 20; i++) {
        int item = buffer_get();
        printf("Consumer [PID=%d] consumed: %d\n", myproc()->pid, item);
        
        // 模拟消费延迟
        for (volatile int j = 0; j < 150000; j++);
    }
    
    printf("Consumer [PID=%d] finished\n", myproc()->pid);
    proc_exit(0);
}

// ==================== 测试函数 ====================

void test_process_creation(void) {
    printf("\n========== Testing process creation ==========\n");

    // 测试基本的进程创建
    printf("Test 1: Basic process creation\n");
    int pid = proc_create(simple_task);
    printf("Created process with PID=%d\n", pid);
    assert(pid > 0, "Failed to create process");
    
    // 等待进程完成
    int status;
    int waited_pid = wait_process(&status);
    printf("Process %d exited with status %d\n", waited_pid, status);

    // 测试进程表限制
    printf("\nTest 2: Process table limits\n");
    // int pids[NPROC];
    
    int count = 0;
    for (int i = 0; i < NPROC + 5; i++) {
        int pid = proc_create(simple_task);
        if (pid > 0) {
            count++;    // 原来是 pids[count++] = pid;
        } else {
            break;
        }
    }
    printf("Successfully created %d processes\n", count);

    // 清理测试进程
    printf("Waiting for all processes to complete...\n");
    for (int i = 0; i < count; i++) {
        wait_process(NULL);
    }
    printf("All processes completed\n");
    
    printf("========== Process creation test PASSED ==========\n\n");
}

void test_scheduler(void) {
    printf("\n========== Testing scheduler ==========\n");

    // 创建多个计算密集型进程
    printf("Creating 3 CPU-intensive processes...\n");
    for (int i = 0; i < 3; i++) {
        int pid = proc_create(cpu_intensive_task);
        printf("Created CPU-intensive process PID=%d\n", pid);
    }

    // 观察调度行为
    uint64 start_time = timer_get_ticks();
    printf("Waiting for processes to complete...\n");
    
    // 等待所有子进程完成
    for (int i = 0; i < 3; i++) {
        wait_process(NULL);
    }
    
    uint64 end_time = timer_get_ticks();

    printf("Scheduler test completed in %lu ticks\n", end_time - start_time);
    printf("========== Scheduler test PASSED ==========\n\n");
}

void test_synchronization(void) {
    printf("\n========== Testing synchronization ==========\n");
    
    // 初始化共享缓冲区
    shared_buffer_init();
    printf("Shared buffer initialized\n");

    // 创建生产者和消费者
    printf("Creating producer and consumer processes...\n");
    int pid1 = proc_create(producer_task);
    int pid2 = proc_create(consumer_task);
    printf("Producer PID=%d, Consumer PID=%d\n", pid1, pid2);

    // 等待完成
    printf("Waiting for producer and consumer to finish...\n");
    wait_process(NULL);
    wait_process(NULL);

    printf("========== Synchronization test PASSED ==========\n\n");
}

// ==================== 主函数 ====================

void run_proc_tests(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Process Management Test Suite       ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");

    // 测试 1: 进程创建
    test_process_creation();
    
    // 测试 2: 调度器
    test_scheduler();
    
    // 测试 3: 同步机制
    test_synchronization();
    
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   All Tests PASSED!                    ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
}
