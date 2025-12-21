// shm.c
#include "dev/console.h"
#include "lib/str.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/shm.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "common.h"
#include "memlayout.h"
#include "riscv.h"

struct shm_table shm_table;

// shm_init() 函数保持不变...
void shm_init(void) {
  spinlock_init(&shm_table.lock, "shm_table");
  for (struct shm_segment *seg = shm_table.segments; seg < &shm_table.segments[MAX_SHM_SEGS]; seg++) {
    spinlock_init(&seg->lock, "shm_segment");
    seg->key = 0;
    seg->num_pages = 0;
    seg->ref_count = 0;
  }
}

// 新的 shm_get 实现
uint64 shm_get(int key, uint32 size) {
  struct proc *p = myproc();
  struct shm_segment *seg = 0;
  uint32 num_pages;

  if (key == 0 || size == 0) return 0;

  num_pages = PG_ROUND_UP(size) / PGSIZE;
  if (num_pages > MAX_PAGES_PER_SEG) return 0; // 请求大小超过上限

  // 1. 查找段
  spinlock_acquire(&shm_table.lock);
  for (seg = shm_table.segments; seg < &shm_table.segments[MAX_SHM_SEGS]; seg++) {
    if (seg->key == key) {
      spinlock_acquire(&seg->lock);
      spinlock_release(&shm_table.lock);
      goto found;
    }
  }

  // 2. 没找到，创建新段
  for (seg = shm_table.segments; seg < &shm_table.segments[MAX_SHM_SEGS]; seg++) {
    if (seg->key == 0) {
      // 2.1 逐页分配物理内存
      for (int i = 0; i < num_pages; i++) {
        void* pa = pmem_alloc();
        if (pa == 0) {
          // 内存不足，回滚已分配的页
          for (int j = 0; j < i; j++) {
            pmem_free(seg->pa_list[j]);
          }
          spinlock_release(&shm_table.lock);
          return 0;
        }
        seg->pa_list[i] = (uint64)pa;
      }

      // 2.2 初始化段信息
      seg->key = key;
      seg->num_pages = num_pages;
      seg->ref_count = 1;
      spinlock_acquire(&seg->lock);
      spinlock_release(&shm_table.lock);
      goto map_it;
    }
  }
  spinlock_release(&shm_table.lock);
  return 0; // 表已满

found:
  // 找到了现有段
  if (seg->num_pages != num_pages) {
    spinlock_release(&seg->lock);
    return 0; // 大小不匹配
  }
  seg->ref_count++;

map_it:
  // 3. 逐页映射到当前进程的虚拟地址空间
  uint64 va = p->heap_top; // 从堆顶开始映射
  for (int i = 0; i < seg->num_pages; i++) {
    pte_t* pte = vm_getpte(p->pgtbl, va + i * PGSIZE, false);
    if(*pte & PTE_V) vm_unmappages(p->pgtbl, va + i * PGSIZE, PGSIZE, true);
    vm_mappages(p->pgtbl, va + i * PGSIZE, seg->pa_list[i], PGSIZE, PTE_W | PTE_R | PTE_U);
  }

  p->heap_top += num_pages * PGSIZE;
  spinlock_release(&seg->lock);
  return va;
}

// 新的 shm_spinlock_release 实现
// 注意：我们通过 key 来释放，因为通过虚拟地址查找非连续页的段会很低效
int shm_spinlock_release(int key) {
  struct proc *p = myproc();
  struct shm_segment *seg = 0;

  if (key == 0) return -1;

  // 1. 查找段
  spinlock_acquire(&shm_table.lock);
  for (seg = shm_table.segments; seg < &shm_table.segments[MAX_SHM_SEGS]; seg++) {
    if (seg->key == key) {
      spinlock_acquire(&seg->lock);
      spinlock_release(&shm_table.lock);
      goto found;
    }
  }
  spinlock_release(&shm_table.lock);
  return -1; // 未找到

found:
  // 2. 找到虚拟地址并解除映射
  // 这个查找过程比较低效，需要遍历页表
  // 更好的设计是在proc结构体中记录映射信息，但这里我们用一个简单直接的方法
  uint64 va;
  int found_in_proc = 0;
  for(va = 0; va < p->heap_top; va += PGSIZE){
      uint64 *pte = vm_getpte(p->pgtbl, va, 0);
      if(pte && (*pte & PTE_V) && PTE2PA(*pte) == seg->pa_list[0]){
          // 找到了！假设起始虚拟地址对应共享段的第一个物理页
          vm_unmappages(p->pgtbl, va, seg->num_pages, 0);
          found_in_proc = 1;
          break;
      }
  }

  if(!found_in_proc){
      // 进程可能已经退出，或者地址空间出了问题
      // 无论如何，我们继续减少引用计数
  }


  // 3. 减少引用计数
  seg->ref_count--;

  // 4. 如果引用计数为0，释放所有物理页
  if (seg->ref_count == 0) {
    for (int i = 0; i < seg->num_pages; i++) {
      pmem_free(seg->pa_list[i]);
    }
    seg->key = 0; // 标记为空闲
  }

  spinlock_release(&seg->lock);
  return 0;
}
