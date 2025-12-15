# 实验8的修改和作用（相比原来的修改）

注：没有新增新的文件，只是在原有的文件上有了更改

## 1. 实现进程优先级调度

### 0. 优先级设置宏定义
在进程的定义文件proc.h中新增了静态优先级和动态优先级，并设计静态和动态的优先级

```c

// 进程优先级范围设置：数值越大，优先级越高
#define PRIORITY_MIN 0
#define PRIORITY_MAX 15
#define PRIORITY_DEFAULT 8

// 进程定义
typedef struct proc {
    // ......

    // 扩展：进程的优先级
    int priority;          // 静态优先级，由用户或内核指定
    int priority_boost;    // 动态加权值，用于简单老化避免饥饿
} proc_t;
```

进程创建的时候，会设置默认优先级以及0的优先级增量：

```c
  p->priority = PRIORITY_DEFAULT;
  p->priority_boost = 0;
```

### 1. `effective_priority` 函数
**功能**：计算进程当前的有效优先级。

```c
static inline int effective_priority( proc_t* p ) {
    // 计算有效优先级 = 静态基础优先级 + 动态提升值(boost)
    // 作用：决定调度时的权重，boost 用于解决饥饿问题。
    int eff = p->priority + p->priority_boost;

    // 边界检查：如果计算出的优先级超过了系统定义的最大值
    if ( eff > PRIORITY_MAX ) {
        // 将其截断为最大值，防止数值溢出或越界
        eff = PRIORITY_MAX;
    }
    // 返回最终计算出的有效优先级
    return eff;
}
```

---

### 2. `boost_waiting_processes` 函数
**功能**：对处于 `RUNNABLE`（就绪）状态但未被选中的进程进行“老化（Aging）”处理，提升其优先级，防止饥饿。

```c
static void boost_waiting_processes( proc_t* last_run  ) {
    // 遍历进程表中的每一个进程槽位
    for ( int idx = 0; idx < NPROC; idx++ ) {
        // 获取当前索引对应的进程结构体指针
        proc_t* p = &proc[idx];

        // 如果该进程是刚刚运行完的进程（last_run）
        if ( p == last_run ) {
            // 跳过它，不给刚运行过的进程提升优先级（因为它刚获得过CPU资源）
            continue;
        }

        // 如果进程状态不是 RUNNABLE（例如它是 UNUSED, SLEEPING, ZOMBIE）
        if ( p->state != RUNNABLE ) {
            // 跳过它，只有在就绪队列里等待的进程才需要提升
            continue;
        }

        // 计算距离最大优先级的剩余空间
        int max_boost = PRIORITY_MAX - p->priority;
        
        // 如果当前的 boost 值还没达到上限
        if ( p->priority_boost < max_boost ) {
            // 增加该进程的 boost 值
            // 作用：随着时间推移，等待越久的进程有效优先级越高，最终会被调度
            p->priority_boost++;
        }
    }
}
```

---

### 3. `proc_scheduler` 函数
**功能**：核心调度循环。负责选择下一个最合适的进程并切换上下文。

```c
// 调度器入口函数，通常由每个 CPU 的启动代码调用
void proc_scheduler()
{
    struct proc *p;
    struct cpu *c = mycpu(); // 获取当前 CPU 的结构体指针

    c->proc = 0; // 初始化当前 CPU 运行的进程为空
    
    // 调度器的无限循环，只要系统在运行，这个循环就不会停止
    for(;;){
        // 打开中断
        // 作用：防止死锁。如果调度器一直关中断循环且找不到进程，外部设备（如磁盘、定时器）将无法响应，系统会挂死。
        intr_on();

        proc_t* chosen = 0; // 用于记录当前选中的最佳进程
        int best_priority = PRIORITY_MIN - 1; // 初始化最佳优先级为最小值（比最小还小，确保第一个遇到的进程能更新它）

        // --- 寻找最佳进程的循环 ---
        for(p = proc; p < &proc[NPROC]; p++) {
            // 获取进程锁
            // 作用：保护进程状态（p->state），防止其他 CPU 同时修改它
            spinlock_acquire(&p->lk);

            // 如果进程不是就绪状态（不能运行）
            if(p->state != RUNNABLE) {
                spinlock_release(&p->lk); // 释放锁
                continue; // 继续找下一个
            }

            // 计算该进程的有效优先级
            int eff = effective_priority( p );
            
            // 核心调度算法：选择逻辑
            // 1. !chosen: 还没选中过进程，直接选中当前这个
            // 2. eff > best_priority: 当前进程优先级比之前选中的更高
            // 3. (eff == best_priority && p->pid < chosen->pid): 优先级相同，选择 PID 更小的（通常意味着更老的进程，作为 Tie-breaker）
            if ( !chosen || eff > best_priority || 
                ( eff == best_priority && p->pid < chosen->pid ) ) {
                chosen = p;          // 更新最佳候选人
                best_priority = eff; // 更新最佳优先级
            }
            // 注意：这里释放了锁。这意味着在循环继续时，chosen 指向的进程状态理论上可能发生变化（但在 xv6 简单实现中通常假设它是安全的，或者在后面再次检查）
            spinlock_release(&p->lk);
        }

        // --- 检查是否找到了进程 ---
        if ( chosen == 0 ) {
            // 如果遍历一圈没找到任何可运行进程
            // 调用汇编指令 wfi (Wait For Interrupt)
            // 作用：让 CPU 进入低功耗休眠状态，直到有中断（如时钟中断）将其唤醒，避免空转浪费电力
            asm volatile( "wfi" );
            continue; // 醒来后重新开始大循环
        }

        // --- 准备切换进程 ---
        // 重新获取选中进程的锁，准备修改其状态并运行
        spinlock_acquire(&chosen->lk);
            
        // 将进程状态修改为 RUNNING，表示正在运行
        chosen->state = RUNNING;
        
        // 关键点：清零 priority_boost
        // 作用：该进程已经获得了 CPU 时间，其积累的“饥饿度”清零，下次调度回归基础优先级
        chosen->priority_boost = 0;
        
        // 记录当前 CPU 正在运行这个进程
        c->proc = chosen;
        
        // 上下文切换 (Context Switch)
        // 作用：保存当前调度器的寄存器上下文到 c->ctx，加载 chosen 进程的上下文 chosen->ctx
        // 此时 CPU 跳转到 chosen 进程的代码执行。
        // 当 chosen 进程未来调用 yield(), sleep() 或 exit() 时，swtch 会返回到这里。
        swtch(&c->ctx, &chosen->ctx);

        // --- 进程让出 CPU 后回到这里 ---
        
        // 获取刚才运行过的进程指针（此时它已经不再运行了）
        proc_t* last_run = myproc(); // 注意：这里通常应该直接用 chosen，或者 c->proc 在清空前的值
        
        // 清空 CPU 的当前进程指针
        c->proc = 0;

        // 释放进程锁（对应上面 switch 前的 acquire）
        spinlock_release(&chosen->lk);

        // 调用老化机制
        // 作用：在每一轮调度结束时，提升那些在就绪队列里等待的进程的 boost 值
        // 这里的 last_run 传入是为了避免给刚才运行过的进程提升优先级
        boost_waiting_processes(last_run);
        
    }
}
```
这段代码实现了一个**基于优先级的抢占式调度算法**，并包含以下关键特性：
1.  **优先级计算**：`Base Priority` + `Aging Boost`。
2.  **防饥饿（Starvation Prevention）**：通过 `boost_waiting_processes`，如果一个进程长期得不到调度，其优先级会动态升高，最终超过高优先级的进程。
3.  **确定性规则**：当优先级相同时，使用 PID 较小的进程（先来先服务/更老的进程）作为决胜条件。
4.  **低功耗设计**：当没有进程可运行时，使用 `wfi` 指令休眠 CPU。

### 4. 设置和返回优先级函数的实现

```c
int proc_set_priority(int priority)
{
    // 如果优先级不合法或当前没进程，返回-1
    if ( priority < PRIORITY_MIN || priority > PRIORITY_MAX || myproc() == 0 ) {
        return -1;
    }

    myproc()->priority = priority;
    myproc()->priority_boost = 0;

    return 0;
}

int proc_get_priority()
{
    // 如果当前没进程，返回-1
    if (myproc() == 0) {
        return -1;
    }

    return myproc()->priority;
}
```

## 2. 扩充了3个与进程优先级相关的系统调用

在系统调用表上的体现
```c
    [SYS_setprior]      sys_setprior,
    [SYS_getprior]      sys_getprior,
    [SYS_yield]         sys_yield,
```

在系统调用实现函数上的实现
```c
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
```