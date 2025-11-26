#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/str.h"
#include "dev/console.h"
#include "common.h"
#include "riscv.h"
#include "memlayout.h"

// 物理页节点
typedef struct page_node{
    struct page_node* next;
} page_node_t;

// 许多物理页构成一个可分配的区域
typedef struct alloc_region { 
    uint64 begin;           // 起始物理地址
    uint64 end;             // 终止物理地址
    spinlock_t lk;          // 自旋锁(保护下面两个变量) 
    uint32 allocable;       // 可分配页面数
    page_node_t list_head;  // 可分配链的链头节点
}alloc_region_t;            // 内核和用户可分配的物理页分开
 
static alloc_region_t free_region;

// 分别对kernel和user下的可分配区域进行初始化操作
void  pmem_init(void){
    free_region.begin = (uint64)&ALLOC_BEGIN;
    free_region.end = PHYSTOP;

    free_region.allocable = 0;
    free_region.list_head.next = NULL;
    spinlock_init(&free_region.lk, "kern_pmem_lock");
    for (uint64 addr = free_region.begin; addr < free_region.end; addr += PGSIZE) {
        // 将物理页首地址转换为 page_node_t 指针
        page_node_t *page = (page_node_t *)addr;
        // 使用头插法插入链表
        page->next = free_region.list_head.next;
        free_region.list_head.next = page;
        free_region.allocable++; // 增加可分配页数
    }
}

void* pmem_alloc(void){
    spinlock_acquire(&free_region.lk); // 上锁

    if(free_region.allocable == 0){
        spinlock_release(&free_region.lk);
        panic("alloc pages not enough");
        return NULL; // 无可用页
    }

    page_node_t *page = free_region.list_head.next;
    if(page)
        free_region.list_head.next = page->next;
    free_region.allocable--;

    spinlock_release(&free_region.lk); // 解锁

    return (void*)page;
}

// 这个page是物理地址
void  pmem_free(uint64 page){

    if((page % PGSIZE) != 0 || (char*)page < ALLOC_BEGIN || page >= PHYSTOP)
        panic("kfree");

    page_node_t *page_ptr = (page_node_t *)page;
    
    spinlock_acquire(&free_region.lk); // 上锁

    page_ptr->next = free_region.list_head.next;
    free_region.list_head.next = page_ptr;
    free_region.allocable++;
    
    spinlock_release(&free_region.lk); // 解锁
}