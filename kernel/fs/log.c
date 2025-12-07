#include "fs/log.h"
#include "common.h"
#include "fs/bio.h"
#include "fs/fs.h"
#include "dev/console.h"
#include "lib/str.h"

// 完整的日志系统实现
// 实现写前日志(WAL)以支持崩溃恢复

#define LOGSIZE 30  // 最大日志块数

struct logheader {
    int n;
    int block[LOGSIZE];
};

struct log {
    int dev;
    int start;
    int size;
    int outstanding;  // 正在进行的文件系统操作数
    int committing;   // 是否正在提交
    
    struct logheader lh;
} log_ctx;

static void recover_from_log(void);
static void commit(void);
static void write_log(void);
static void write_head(void);
static void install_trans(int recovering);
static void read_head(void);

void log_init(int dev, struct superblock *sb) {
    if(sizeof(struct logheader) >= BSIZE)
        panic("log_init: too big logheader");
    
    log_ctx.dev = dev;
    log_ctx.start = sb->logstart;
    log_ctx.size = sb->nlog;
    
    recover_from_log();
}

// 从磁盘读取日志头
static void read_head(void) {
    struct buf *buf = bread(log_ctx.dev, log_ctx.start);
    struct logheader *lh = (struct logheader *)(buf->data);
    int i;
    
    log_ctx.lh.n = lh->n;
    for(i = 0; i < log_ctx.lh.n; i++) {
        log_ctx.lh.block[i] = lh->block[i];
    }
    brelse(buf);
}

// 将内存中的日志头写入磁盘
// 这是提交点,在此之前崩溃,日志无效;在此之后崩溃,日志有效
static void write_head(void) {
    struct buf *buf = bread(log_ctx.dev, log_ctx.start);
    struct logheader *hb = (struct logheader *)(buf->data);
    int i;
    
    hb->n = log_ctx.lh.n;
    for(i = 0; i < log_ctx.lh.n; i++) {
        hb->block[i] = log_ctx.lh.block[i];
    }
    bwrite(buf);
    brelse(buf);
}

// 将修改的块从缓存复制到日志
static void write_log(void) {
    int tail;
    
    for(tail = 0; tail < log_ctx.lh.n; tail++) {
        struct buf *to = bread(log_ctx.dev, log_ctx.start + tail + 1);  // 日志块
        struct buf *from = bread(log_ctx.dev, log_ctx.lh.block[tail]);  // 缓存块
        memmove(to->data, from->data, BSIZE);
        bwrite(to);  // 写入日志
        brelse(from);
        brelse(to);
    }
}

// 将日志中的块安装到它们在文件系统中的实际位置
static void install_trans(int recovering) {
    int tail;
    
    for(tail = 0; tail < log_ctx.lh.n; tail++) {
        struct buf *lbuf = bread(log_ctx.dev, log_ctx.start + tail + 1);  // 读取日志块
        struct buf *dbuf = bread(log_ctx.dev, log_ctx.lh.block[tail]);    // 读取目标块
        memmove(dbuf->data, lbuf->data, BSIZE);  // 复制数据
        bwrite(dbuf);  // 写入磁盘
        if(!recovering)
            bunpin(dbuf);
        brelse(lbuf);
        brelse(dbuf);
    }
}

// 从日志恢复
// 在系统启动时调用,在第一次用户进程运行之前
static void recover_from_log(void) {
    read_head();
    install_trans(1);  // 如果已提交,则安装
    log_ctx.lh.n = 0;
    write_head();  // 清除日志
}

// 开始文件系统操作
void begin_op(void) {
    while(1) {
        if(log_ctx.committing) {
            // 等待提交完成
            continue;
        } else if(log_ctx.lh.n + (log_ctx.outstanding + 1) * LOGSIZE > LOGSIZE) {
            // 日志空间不足
            continue;
        } else {
            log_ctx.outstanding++;
            break;
        }
    }
}

// 结束文件系统操作
void end_op(void) {
    int do_commit = 0;
    
    log_ctx.outstanding--;
    
    if(log_ctx.committing)
        panic("log_ctx.committing");
    
    if(log_ctx.outstanding == 0) {
        do_commit = 1;
        log_ctx.committing = 1;
    }
    
    if(do_commit) {
        commit();
        log_ctx.committing = 0;
    }
}

// 将修改后的块写入日志
void log_write(struct buf *b) {
    int i;
    
    if(log_ctx.lh.n >= LOGSIZE || log_ctx.lh.n >= log_ctx.size - 1)
        panic("too big a transaction");
    if(log_ctx.outstanding < 1)
        panic("log_write outside of trans");
    
    // 查找块是否已在日志中
    for(i = 0; i < log_ctx.lh.n; i++) {
        if(log_ctx.lh.block[i] == b->blockno)
            break;
    }
    
    log_ctx.lh.block[i] = b->blockno;
    if(i == log_ctx.lh.n) {
        bpin(b);
        log_ctx.lh.n++;
    }
}

// 提交当前事务
// 完整的四步提交协议:
// 1. write_log() - 将修改的块写入日志
// 2. write_head() - 将日志头写入磁盘(提交点)
// 3. install_trans() - 将日志块安装到实际位置
// 4. write_head() - 清除日志头(标记事务完成)
static void commit(void) {
    if(log_ctx.lh.n > 0) {
        write_log();      // 写入修改的块到日志
        write_head();     // 写入日志头到磁盘 - 提交点
        install_trans(0); // 安装日志到实际位置
        log_ctx.lh.n = 0;
        write_head();     // 清除日志
    }
}

