## pmem.c

这段代码实现了操作系统内核中的**物理内存分配器（Physical Memory Allocator）**。

它采用了一种经典的**空闲链表（Free List）**算法来管理物理内存。当内核需要内存时，它从链表中取出一个页；当内存不再使用时，它将该页放回链表。

下面我将分模块详细解读每一行代码的功能。

### 1. 数据结构定义

```c
// 物理页节点
// 这是一个"侵入式链表"节点。
// 当一个物理页空闲时，我们不需要额外的内存来记录它，
// 直接利用这个物理页的前8个字节存储指向下一个空闲页的指针。
typedef struct page_node{
    struct page_node* next;
} page_node_t;

// 物理内存区域管理结构体
typedef struct alloc_region { 
    uint64 begin;           // 可分配区域的起始物理地址
    uint64 end;             // 可分配区域的结束物理地址
    spinlock_t lk;          // 自旋锁：保护链表，防止多核CPU同时申请内存导致竞争
    uint32 allocable;       // 计数器：当前剩余多少个空闲页
    page_node_t list_head;  // 链表的哨兵头节点（本身不代表物理页，只指向第一个真的空闲页）
} alloc_region_t;

// 全局唯一的物理内存管理器实例
static alloc_region_t free_region;
```

### 2. 初始化函数 (`pmem_init`)

这个函数在系统启动时调用一次，负责将所有可用的物理内存切分成 4KB 的页，并串联成一个链表。

```c
void pmem_init(void){
    // 设定起始地址。ALLOC_BEGIN 通常定义在链接脚本中，指向内核代码段结束后的位置
    free_region.begin = (uint64)&ALLOC_BEGIN;
    // 设定结束地址。PHYSTOP 是物理内存的上限（例如 128MB）
    free_region.end = PHYSTOP;

    // 初始化计数器和链表头
    free_region.allocable = 0;
    free_region.list_head.next = NULL;
    
    // 初始化锁，用于后续并发保护
    spinlock_init(&free_region.lk, "kern_pmem_lock");

    // 核心循环：从起始地址开始，每次步进一页的大小 (PGSIZE = 4096字节)
    for (uint64 addr = free_region.begin; addr < free_region.end; addr += PGSIZE) {
        // 强制类型转换：将当前的物理地址 addr 当作一个 page_node_t 结构体指针
        // 这意味着我们将向这块内存的前8个字节写入数据
        page_node_t *page = (page_node_t *)addr;

        // 头插法（Head Insertion）：将当前页插入到链表头部
        // 1. 当前页指向原本的第一个页
        page->next = free_region.list_head.next;
        // 2. 头节点指向当前页
        free_region.list_head.next = page;

        // 记录可用页数加一
        free_region.allocable++; 
    }
}
```

### 3. 分配函数 (`pmem_alloc`)

当内核或用户进程需要一个新的物理页时调用。

```c
void* pmem_alloc(void){
    // 进入临界区，加锁。防止两个CPU同时抢同一个物理页。
    spinlock_acquire(&free_region.lk); 

    // 检查是否有空闲页
    if(free_region.allocable == 0){
        spinlock_release(&free_region.lk); // 必须先解锁再报错，否则死锁
        panic("alloc pages not enough");   // 内存耗尽，内核恐慌（实际OS通常会尝试回收或返回NULL）
        return NULL; 
    }

    // 取出链表中的第一个节点
    page_node_t *page = free_region.list_head.next;
    
    // 将头节点指向下下个节点（从链表中移除 page）
    if(page)
        free_region.list_head.next = page->next;
    
    // 减少计数
    free_region.allocable--;

    // 离开临界区，解锁
    spinlock_release(&free_region.lk); 

    // 返回分配到的物理页地址（void* 通用指针）
    return (void*)page;
}
```

### 4. 释放函数 (`pmem_free`)

当物理页不再使用（例如进程退出、页表销毁）时调用，将其归还给空闲链表。

```c
// 参数 page 是要释放的物理地址
void pmem_free(uint64 page){

    // 安全检查：
    // 1. 地址必须是页对齐的 (4KB倍数)
    // 2. 地址必须在合法范围内 (大于起始点，小于结束点)
    if((page % PGSIZE) != 0 || (char*)page < ALLOC_BEGIN || page >= PHYSTOP)
        panic("kfree");

    // 将物理地址转换为节点指针
    page_node_t *page_ptr = (page_node_t *)page;
    
    // 加锁
    spinlock_acquire(&free_region.lk); 

    // 头插法：将这个页放回链表头部
    // 1. 这个页指向原本的第一个页
    page_ptr->next = free_region.list_head.next;
    // 2. 头节点指向这个页
    free_region.list_head.next = page_ptr;
    
    // 增加计数
    free_region.allocable++;
    
    // 解锁
    spinlock_release(&free_region.lk); 
}
```

### 总结
这个文件 (`pmem.c`) 实现了最底层的**物理内存管理**：
1.  **初始化**：将一大块连续的物理 RAM 切割成无数个 4KB 的小块。
2.  **管理方式**：使用**空闲链表**将所有空闲块串起来。
3.  **分配**：从链表头拿走一块。
4.  **回收**：把一块内存塞回链表头。
5.  **并发安全**：使用**自旋锁**保证多核环境下的安全性。

## kvm.c

这段代码是操作系统内核中**内核虚拟内存管理（Kernel Virtual Memory Management）**的核心实现。

它负责构建内核页表，将物理内存映射到内核的虚拟地址空间，并开启 MMU（内存管理单元）。这使得内核能够通过虚拟地址访问物理内存、硬件设备（MMIO）以及处理中断入口。

下面我将分模块详细解读每一行代码的功能。

### 1. 全局变量与辅助函数

```c
pgtbl_t kernel_pagetable; // 全局变量，保存内核根页表的物理地址

extern char etext[]; // 链接脚本定义的符号，标记内核代码段（.text）的结束位置
extern char trampoline[]; // 汇编代码 trampoline.S 的起始地址

// 递归打印页表内容的辅助函数（调试用）
void vm_print_helper(pgtbl_t pgtbl, int level){
    for(int i=0; i<512; i++){ // 遍历当前页表的 512 个页表项 (PTE)
        pte_t *pte = &pgtbl[i];
        pgtbl_t pa = (pgtbl_t)PTE2PA(*pte); // 获取 PTE 指向的物理地址
        // 如果 PTE 有效且指向合法的物理地址
        if(pte && pa){
            // 根据层级打印缩进
            for(int k=0; k<level; k++) printf("    ");
            // 打印索引、PTE的值、物理地址
            printf("%d: pte %p, pa %p\n", i, pte, (PTE2PA(*pte)));
            // 如果不是最后一级（RISC-V Sv39 有 3 级：2->1->0），递归打印下一级
            if(level < 2)
                vm_print_helper(pa, level+1); 
        }
    }
}

// 对外接口：打印整个页表结构
void vm_print(pgtbl_t pgtbl){
    vm_print_helper(pgtbl, 0);
}
```

### 2. 页表查找 (`vm_getpte`)

这是页表操作中最底层的函数，模拟硬件 MMU 的“页表游走（Page Walk）”过程。

```c
// 给定页表 pgtbl 和虚拟地址 va，返回对应的第 0 级页表项 (PTE) 的指针
// alloc: 如果中间层页表不存在，是否分配新的？
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc){
    // RISC-V Sv39 模式有 3 级页表 (2 -> 1 -> 0)
    // 这里遍历前两级 (Level 2 和 Level 1)
    for(int level = 2; level > 0; level--) {
        // PX(va, level) 宏提取 va 中对应层级的 9 位索引
        pte_t *pte = &pgtbl[PX(va, level)];
        
        // 检查 PTE_V (Valid) 位，看该页表项是否有效
        if(*pte & PTE_V) {
            // 如果有效，说明下一级页表存在，获取其物理地址继续遍历
            pgtbl = (pgtbl_t)PTE2PA(*pte);
        } else {
            // 如果无效（下一级页表不存在）
            // 如果不要求分配，或者内存耗尽分配失败，返回 0
            if(!alloc || (pgtbl = (pte_t*)pmem_alloc()) == 0)
                return 0;
            // 新分配的页表页清零
            memset(pgtbl, 0, PGSIZE);
            // 将新页表的物理地址填入当前 PTE，并设置 Valid 位
            // PA2PTE 宏将物理地址转换为 PTE 格式
            *pte = PA2PTE(pgtbl) | PTE_V;
        }
  }
  // 返回第 0 级页表中对应的 PTE 指针
  return &pgtbl[PX(va, 0)];
}
```

### 3. 建立映射 (`vm_mappages`)

将一段虚拟地址区间映射到一段物理地址区间。

```c
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm){
    uint64 a, last;
    pte_t *pte;

    if(len == 0) panic("mappages: len");

    // 向下对齐起始地址，确保从页边界开始
    a = PG_ROUND_DOWN(va);
    // 计算最后一个字节所在的页边界
    last = PG_ROUND_DOWN(va + len - 1);
    
    for(;;){
        // 获取该虚拟地址对应的 PTE，如果中间缺失则分配 (alloc=1)
        if((pte = vm_getpte(pgtbl, a, 1)) == 0)
            panic("mappages: pte==0"); // 内存不足
        
        // 如果该 PTE 已经被映射过了（PTE_V 为 1），则报错（防止覆盖映射）
        if(*pte & PTE_V)
            panic("mappages: remap");
        
        // 设置 PTE：物理地址 | 权限位 (R/W/X/U) | Valid 位
        *pte = PA2PTE(pa) | perm | PTE_V;
        
        // 循环结束条件
        if(a == last) break;
        
        // 步进一页
        a += PGSIZE;
        pa += PGSIZE;
    }
}
```

### 4. 解除映射 (`vm_unmappages`)

```c
// freeit: 是否释放物理页内存
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit){
    uint64 a;
    pte_t *pte;

    if((va % PGSIZE) != 0) panic("uvmunmap: not aligned");

    // 计算涉及的页数
    int len_rounded = PG_ROUND_UP(len);
    
    for(a = va; a < va + len_rounded; a += PGSIZE){
        // 获取 PTE，不分配 (alloc=0)
        if((pte = vm_getpte(pgtbl, a, 0)) == 0)
            panic("uvmunmap: vm_getpte"); // 页表结构缺失
        if((*pte & PTE_V) == 0)
            panic("uvmunmap: not mapped"); // 页面未映射
        
        // 检查是否是叶子节点（Sv39 中只有叶子节点才有 R/W/X 权限）
        // 如果只有 V 位而没有 R/W/X，说明它指向下一级页表，不是我们要找的数据页
        if(PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        
        // 如果需要释放物理内存
        if(freeit){
            uint64 pa = PTE2PA(*pte);
            pmem_free(pa);
        }
        // 清空 PTE，解除映射
        *pte = 0;
    }
}
```

### 5. 创建内核页表 (`kvm_create`)

这是构建内核地址空间布局的关键函数。它采用了**直接映射 (Direct Mapping)** 的策略，即虚拟地址 = 物理地址（除了 Trampoline 和栈）。

```c
pgtbl_t kvm_create(){
    pgtbl_t kpgtbl;

    // 分配根页表页
    kpgtbl = (pgtbl_t) pmem_alloc();
    memset(kpgtbl, 0, PGSIZE);
    
    // 1. 映射 UART (串口) 寄存器
    // 允许读写，用于 printf 输出
    vm_mappages(kpgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);

    // 2. 映射 CLINT (核心本地中断控制器)
    // 用于时钟中断
    vm_mappages(kpgtbl, CLINT_BASE, CLINT_BASE, PGSIZE, PTE_R | PTE_W);

    // 3. 映射 PLIC (平台级中断控制器)
    // 用于外部设备中断
    vm_mappages(kpgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W);

    // 4. 映射内核代码段 (.text)
    // 范围：KERNEL_BASE (0x80000000) 到 etext
    // 权限：R | X (可读可执行)，没有 W 权限，防止代码被意外修改
    vm_mappages(kpgtbl, KERNEL_BASE, KERNEL_BASE, (uint64)etext-KERNEL_BASE, PTE_R | PTE_X);

    // 5. 映射内核数据段 (data + bss + 剩余物理内存)
    // 范围：etext 到 PHYSTOP (物理内存结束)
    // 权限：R | W (可读可写)，没有 X 权限，防止数据被当成指令执行（安全特性）
    vm_mappages(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

    // 6. 映射 Trampoline (跳板页)
    // 这是一个特殊的映射：将物理上的 trampoline 代码映射到虚拟地址空间的最高处 (TRAMPOLINE)
    // 这是为了在用户态和内核态切换时，保证虚拟地址固定且不与用户空间冲突
    vm_mappages(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 7. 映射内核栈
    // 为每个进程预留并映射内核栈空间
    proc_mapstacks(kpgtbl);

    return kpgtbl;
}
```

### 6. 初始化与启用 (`kvm_init`, `kvm_inithart`)

```c
// 系统启动时调用一次
void kvm_init(){
    kernel_pagetable = kvm_create();
}

// 每个 CPU 核心启动时都要调用
void kvm_inithart(){
    // 内存屏障，确保前面的页表写入操作已经完成
    sfence_vma();

    // 写入 SATP 寄存器 (Supervisor Address Translation and Protection)
    // 1. 设置模式为 Sv39
    // 2. 设置根页表物理地址 (kernel_pagetable)
    // 这一步执行后，CPU 开始使用虚拟地址
    w_satp(MAKE_SATP(kernel_pagetable));

    // 刷新 TLB (Translation Lookaside Buffer)
    // 确保 CPU 不会使用旧的缓存映射
    sfence_vma();
}
```

### 总结
这个文件实现了内核的**内存视图**：
1.  **设备与内存映射**：通过 `kvm_create` 将物理内存和设备寄存器 1:1 映射到内核空间，方便内核直接访问。
2.  **权限控制**：严格区分代码段（R|X）和数据段（R|W），提高安全性。
3.  **特殊映射**：Trampoline 被映射到虚拟地址顶端，这是 RISC-V 处理 Trap 的关键机制。
4.  **硬件启用**：通过 `kvm_inithart` 操作 CSR 寄存器，正式开启分页机制。

## main.c

这段代码是操作系统的**内核入口点（main.c）**，以及用于**测试虚拟内存管理模块（vmem.c）功能的测试套件**。

它展示了内核启动的早期流程：初始化物理内存 -> 初始化内核页表 -> 开启分页 -> 运行测试用例。

下面我将分模块详细解读每一行代码的功能。

### 1. 全局变量与头文件

```c
// ... 头文件包含 ...

// 用于多核启动同步的标志位（虽然在这个单核示例中未被重度使用）
volatile static int started = 0;
```

### 2. 测试用例 1：基本映射测试 (`test_pagetable_1`)

这个函数测试最基本的页表操作：创建页表、映映射结射一个页、验证果。

```c
void test_pagetable_1(void){
    printf("====Pagetable test 1 started====\n\n");

    // 1. 分配一个物理页作为根页表
    pgtbl_t pt = (pgtbl_t) pmem_alloc();
    memset(pt, 0, PGSIZE); // 必须清零，否则会有垃圾数据被当做有效 PTE
 
    // 2. 准备测试数据
    uint64 va = 0x10000000; // 选一个虚拟地址
    uint64 pa = (uint64)pmem_alloc(); // 分配一个物理页作为目标
    
    // 3. 建立映射：VA -> PA，权限 R|W
    vm_mappages(pt, va, pa, PGSIZE, PTE_R | PTE_W); 
 
    // 4. 验证映射是否存在
    // vm_getpte 应该能找到对应的 PTE
    pte_t *pte = vm_getpte(pt, va, true); 
    assert(pte != 0 && (*pte & PTE_V), "Test2"); // 检查 PTE 是否存在且有效
    assert(PTE2PA(*pte) == pa, "Test3"); // 检查物理地址是否正确
 
    // 5. 验证权限位
    assert(*pte & PTE_R, "Test4"); // 应该有读权限
    assert(*pte & PTE_W, "Test5"); // 应该有写权限
    assert(!(*pte & PTE_X), "Test6"); // 不应该有执行权限

    printf("====Pagetable test 1 ended====\n\n");
}
```

### 3. 测试用例 2：复杂映射与解映射 (`test_pagetable_2`)

这个函数测试了不同位置、不同大小、不同权限的映射，以及解映射操作。

```c
void test_pagetable_2(void){
    printf("====Pagetable test 2 started====\n\n");

    pgtbl_t test_pgtbl = pmem_alloc(); // 创建根页表
    uint64 mem[5];
    // 预先分配 5 个物理页
    for(int i = 0; i < 5; i++){
        mem[i] = (uint64)pmem_alloc();
    }

    printf("\nPart1: allocating pages\n\n");    
    // 1. 映射地址 0 (边界测试)
    vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
    
    // 2. 映射非对齐大小 (PGSIZE/2) -> 应该会自动向上取整覆盖一整页
    vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE / 2, PTE_R | PTE_W);
    
    // 3. 跨级映射测试 (PGSIZE * 512 刚好跨越 Level 0 的边界，进入下一个 Level 1 条目)
    vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE - 1, PTE_R | PTE_X);
    
    // 4. 更大的跨级 (PGSIZE * 512 * 512 跨越 Level 1 边界)
    vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[3], PGSIZE, PTE_R | PTE_X);
    
    // 5. 最高地址测试 (MAXVA)
    vm_mappages(test_pgtbl, MAXVA - PGSIZE, mem[4], PGSIZE, PTE_W);
    
    vm_print(test_pgtbl); // 打印页表结构查看结果

    printf("\nPart2: unmappages\n\n");
    // 6. 解除映射并释放物理页 (freeit=true)
    vm_unmappages(test_pgtbl, 0, PGSIZE, true); 
    
    // 7. 重新映射同一个虚拟地址 (测试复用)
    // 注意：这里用 mem[0] 是不安全的，因为上面刚刚 free 了它。
    // 但在单线程且无其他分配干扰的测试环境下，数据可能还在，或者只是为了测试映射逻辑本身。
    vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
    
    // 8. 继续解映射其他部分
    vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
    vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
    
    vm_print(test_pgtbl); // 再次查看

    printf("====Pagetable test 2 ended====\n\n");
}
```

### 4. 测试用例 3：批量压力测试 (`test_pagetable_3`)

这个函数通过循环进行大量映射，测试页表分配器的稳定性和正确性。

```c
void test_pagetable_3(void){
    printf("====Pagetable test 3 started====\n\n");
    
    pgtbl_t test_pgtbl = pmem_alloc();

    // 1. 批量分配 64 个物理页
    uint64 pa_list[64];
    for(int i=0; i<64; i++){
        pa_list[i] = (uint64)pmem_alloc();
        // ... 错误检查 ...
    }

    // 2. 批量映射
    // 这里的逻辑有点特殊：
    // 第 i 次循环，映射长度为 (i+1)*PGSIZE。
    // 这意味着随着 i 增大，映射的范围越来越大，可能会跨越多个页表页。
    for(int i=0; i<64; i++){
        vm_mappages(test_pgtbl, i*64*PGSIZE, pa_list[i], (i+1)*PGSIZE, PTE_R);
        printf("Id %d: Mapped VA %p to PA %p.\n", i, i*64*PGSIZE, pa_list[i]);
    }

    // 3. 验证映射
    for(int i=0; i<64; i++){
        // 检查每个映射的首地址是否正确
        pte_t *pte = vm_getpte(test_pgtbl, i*64*PGSIZE, 0);
        if (pte == 0 || (*pte & PTE_V) == 0) {
            panic("Failed to find correct mapping");
        } else {
            uint64 mapped_pa = PTE2PA(*pte);
            // 验证查到的物理地址是否等于当初分配的地址
            assert(mapped_pa == pa_list[i], "Incorrect mapping");
        }
    }

    // 4. 清理测试
    // 先清除后 32 个
    for(int i=32; i<64; i++){
        vm_unmappages(test_pgtbl, i*64*PGSIZE, (i+1)*PGSIZE, true);
    }
    vm_print(test_pgtbl); 

    // 再倒序清除前 32 个
    for(int i=31; i>=0; i--){
        vm_unmappages(test_pgtbl, i*64*PGSIZE, (i+1)*PGSIZE, true);
    }
    vm_print(test_pgtbl);
    
    printf("====Pagetable test 3 ended====\n\n");
}
```

### 5. 主函数 (`main`)

这是内核启动后的 C 语言入口。

```c
void main()
{
    // 1. 初始化物理内存分配器
    // 此时可以使用 pmem_alloc() 了
    pmem_init();
    
    // 2. 初始化内核页表
    // 创建内核页表结构，映射内核代码、数据和设备
    kvm_init();
    
    // 3. 开启分页机制
    // 写入 satp 寄存器，从物理寻址切换到虚拟寻址
    kvm_inithart();

    printf("Hello OS\n"); // 第一个输出，证明 UART 映射成功

    printf("Activating KVM success. Now testing...\n");
    
    // 4. 运行单元测试
    test_pagetable_1();
    test_pagetable_2();
    test_pagetable_3();

    // 5. 死循环
    // 防止 main 函数返回（OS 内核不应该退出）
    while(1);
}
```

### 总结
这个文件 (`main.c`) 是一个**早期的内核启动与测试脚手架**。
*   它不包含进程调度、文件系统等高级功能。
*   它的主要目的是验证**内存管理子系统 (pmem + vmem)** 是否工作正常。
*   如果这三个测试都能通过，说明内核的内存基石已经打好，可以开始实现进程管理了。

