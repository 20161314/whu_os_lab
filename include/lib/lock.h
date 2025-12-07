#ifndef __LOCK_H__
#define __LOCK_H__

#include "common.h"

typedef struct spinlock {
    int locked;
    char* name;
    int cpuid;
} spinlock_t;

typedef struct sleeplock {
  int locked;           
  struct spinlock lk;   
  char *name;           
  int pid;              
} sleeplock_t;

void push_off();
void pop_off();

// 自旋锁相关函数
void spinlock_init(spinlock_t* lk, char* name);
void spinlock_acquire(spinlock_t* lk);
void spinlock_release(spinlock_t* lk);
bool spinlock_holding(spinlock_t* lk);

// 睡眠锁相关函数
void sleeplock_init(sleeplock_t* slk, char *name);
void sleeplock_acquire(sleeplock_t* slk);
void sleeplock_release(sleeplock_t* slk);
bool sleeplock_holding(sleeplock_t* slk);

#endif