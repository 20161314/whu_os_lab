#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"

// 来自kernel.ld
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];

void  pmem_init(void);
void* pmem_alloc(void);
void  pmem_free(uint64 page);

#endif