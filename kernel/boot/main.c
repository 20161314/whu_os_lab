#include "riscv.h"
#include "dev/console.h"
#include "lib/str.h"
#include "proc/proc.h"
#include "mem/pmem.h"
#include "mem/vmem.h"

volatile static int started = 0;

void test_pagetable_1(void){
    printf("====Pagetable test 1 started====\n\n");

    pgtbl_t pt = (pgtbl_t) pmem_alloc();
    memset(pt, 0, PGSIZE);
 
    // 测试基本映射 
    uint64 va = 0x10000000; 
    uint64 pa = (uint64)pmem_alloc(); 
    vm_mappages(pt, va, pa, PGSIZE, PTE_R | PTE_W); 
 
    // 测试地址转换 
    pte_t *pte = vm_getpte(pt, va, true); 
    assert(pte != 0 && (*pte & PTE_V), "Test2"); 
    assert(PTE2PA(*pte) == pa, "Test3"); 
 
    // 测试权限位 
    assert(*pte & PTE_R, "Test4"); 
    assert(*pte & PTE_W, "Test5"); 
    assert(!(*pte & PTE_X), "Test6");

    printf("====Pagetable test 1 ended====\n\n");
}

void test_pagetable_2(void){
    printf("====Pagetable test 2 started====\n\n");

    pgtbl_t test_pgtbl = pmem_alloc();
    uint64 mem[5];
    for(int i = 0; i < 5; i++){
        mem[i] = (uint64)pmem_alloc();
    }

    printf("\nPart1: allocating pages\n\n");    
    vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_R);
    vm_mappages(test_pgtbl, PGSIZE * 10, mem[1], PGSIZE / 2, PTE_R | PTE_W);
    vm_mappages(test_pgtbl, PGSIZE * 512, mem[2], PGSIZE - 1, PTE_R | PTE_X);
    vm_mappages(test_pgtbl, PGSIZE * 512 * 512, mem[3], PGSIZE, PTE_R | PTE_X);
    vm_mappages(test_pgtbl, MAXVA - PGSIZE, mem[4], PGSIZE, PTE_W);
    vm_print(test_pgtbl); printf("\n");

    
    printf("\nPart2: unmappages\n\n");
    vm_unmappages(test_pgtbl, 0, PGSIZE, true); 
    // 由于下行代码重复分配了，所以需要先解绑再重绑
    vm_mappages(test_pgtbl, 0, mem[0], PGSIZE, PTE_W);
    vm_unmappages(test_pgtbl, PGSIZE * 10, PGSIZE, true);
    vm_unmappages(test_pgtbl, PGSIZE * 512, PGSIZE, true);
    vm_print(test_pgtbl);

    printf("====Pagetable test 2 ended====\n\n");
}

// 测试64个页表分配和映射（不同的分配量和地址）
void test_pagetable_3(void){
    printf("====Pagetable test 3 started====\n\n");
    
    // 创建页表
    pgtbl_t test_pgtbl = pmem_alloc();

    // 创建物理页
    uint64 pa_list[64];
    for(int i=0; i<64; i++){
        pa_list[i] = (uint64)pmem_alloc();
        if (pa_list[i] == 0) {
            printf("Failed to allocate physical memory.\n");
            return;
        }
    }

    // 开始映射
    // 第i个项，虚拟地址为i*64*PGSIZE, 物理地址为pa_list[i], 分配大小(i+1)*PGSIZE 
    for(int i=0; i<64; i++){
        vm_mappages(test_pgtbl, i*64*PGSIZE, pa_list[i], (i+1)*PGSIZE, PTE_R);
        printf("Id %d: Mapped VA %p to PA %p.\n", i, i*64*PGSIZE, pa_list[i]);
    }

    // vm_print(test_pgtbl); printf("\n");

    // 验证映射是否正确
    for(int i=0; i<64; i++){
        pte_t *pte = vm_getpte(test_pgtbl, i*64*PGSIZE, 0);
        if (pte == 0 || (*pte & PTE_V) == 0) {
            panic("Failed to find correct mapping");
        } else {
            uint64 mapped_pa = PTE2PA(*pte);

            printf("VA 0x%p is mapped to PA 0x%p.\n", i*64*PGSIZE, mapped_pa);

            assert(mapped_pa == pa_list[i], "Incorrect mapping");
        }
    }

    // 清除后32个页面
    for(int i=32; i<64; i++){
        vm_unmappages(test_pgtbl, i*64*PGSIZE, (i+1)*PGSIZE, true);
    }

    vm_print(test_pgtbl); printf("\n");

    // 倒序清除前32个页面
    for(int i=31; i>=0; i--){
        vm_unmappages(test_pgtbl, i*64*PGSIZE, (i+1)*PGSIZE, true);
    }

    vm_print(test_pgtbl); printf("\n");
    
    printf("====Pagetable test 3 ended====\n\n");
}

void main()
{
    pmem_init();
    kvm_init();
    kvm_inithart();

    printf("Hello OS\n");

    printf("Activating KVM success. Now testing...\n");
    test_pagetable_1();
    test_pagetable_2();
    test_pagetable_3();

    while(1);
}