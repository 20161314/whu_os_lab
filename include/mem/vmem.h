#ifndef __KVMEM_H__
#define __KVMEM_H__

#include "common.h"

// 页表项
typedef uint64 pte_t;

// 顶级页表
typedef uint64* pgtbl_t;

// satp寄存器相关
#define SATP_SV39 (8L << 60)  // MODE = SV39
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12)) // 设置MODE和PPN字段

void   vm_print(pgtbl_t pgtbl);
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
uint64 vm_getpa(pgtbl_t pgtbl, uint64 va);
void   vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void   vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);

pgtbl_t kvm_create(void);
void    kvm_init();
void    kvm_inithart();

/*------------------------ in uvm.c -----------------------*/

void   uvm_destroy_pgtbl(pgtbl_t pgtbl);
void   uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages);

uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len);
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len);

int    uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
int    uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len);
void   uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen);

#endif