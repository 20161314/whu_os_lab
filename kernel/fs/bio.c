#include "fs/bio.h"
#include "common.h"
#include "dev/console.h"
#include "lib/str.h"
#include "dev/virtio.h"

// 块缓存数量
#define NBUF 30

struct {
    struct buf buf[NBUF];
    
    // LRU 链表头，按照 next 顺序最近最少使用
    struct buf head;
} bcache;

// 简化版本：不使用真实磁盘，使用内存模拟
// 为简化实现，我们创建一个内存块数组来模拟磁盘
void bio_init(void) {
    struct buf *b;
    
    // 初始化 LRU 链表
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    
    for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
    
    virtio_disk_init();
}

// 查找缓存块，如果不存在则分配一个
static struct buf* bget(uint64 dev, uint64 blockno) {
    struct buf *b;
    
    // 块号是否有效
    if(blockno >= FSSIZE) {
        panic("bget: blockno out of range");
    }
    
    // 查找块是否已经在缓存中
    for(b = bcache.head.next; b != &bcache.head; b = b->next) {
        if(b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            return b;
        }
    }
    
    // 未找到，寻找未使用的缓冲区
    // 从尾部开始查找（最近最少使用）
    for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {
        if(b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            return b;
        }
    }
    
    panic("bget: no buffers");
    return 0;
}

// 读取块
struct buf* bread(uint64 dev, uint64 blockno) {
    struct buf *b;
    
    b = bget(dev, blockno);
    
    if(!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    
    return b;
}

// 写入块到磁盘
void bwrite(struct buf *b) {
    if(b->refcnt < 1)
        panic("bwrite");
    
    virtio_disk_rw(b, 1);
}

// 释放块
void brelse(struct buf *b) {
    if(b->refcnt < 1)
        panic("brelse");
    
    b->refcnt--;
    
    if(b->refcnt == 0) {
        // 移动到 LRU 链表头部
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }
}

// 增加引用计数（用于日志系统）
void bpin(struct buf *b) {
    b->refcnt++;
}

// 减少引用计数（用于日志系统）
void bunpin(struct buf *b) {
    b->refcnt--;
}
