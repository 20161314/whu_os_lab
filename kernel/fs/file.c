#include "common.h"
#include "fs/fs.h"
#include "fs/log.h"
#include "fs/file.h"
#include "dev/console.h"
#include "lib/str.h"

struct ftable_s ftable;

void file_init(void) {
    // 初始化文件表
    for(int i = 0; i < NFILE; i++) {
        ftable.file[i].ref = 0;
        ftable.file[i].type = FILE_NONE;
    }
}

// 分配文件结构
struct File* alloc_file(void) {
    for(int i = 0; i < NFILE; i++) {
        if(ftable.file[i].ref == 0) {
            ftable.file[i].ref = 1;
            ftable.file[i].type = FILE_NONE;
            ftable.file[i].readable = 0;
            ftable.file[i].writable = 0;
            ftable.file[i].append = 0;
            ftable.file[i].ip = 0;
            ftable.file[i].off = 0;
            return &ftable.file[i];
        }
    }
    return 0;
}

// 增加文件引用计数
struct File* file_dup(struct File *f) {
    if(f->ref < 1)
        panic("file_dup");
    f->ref++;
    return f;
}

// 关闭文件
void file_close(struct File *f) {
    if(f->ref < 1)
        panic("file_close");
    
    if(--f->ref > 0)
        return;
    
    struct File ff = *f;
    f->ref = 0;
    f->type = FILE_NONE;
    
    if(ff.type == FILE_INODE) {
        begin_op();
        iput(ff.ip);
        end_op();
    }
}

// 获取文件状态（简化版本）
int file_stat(struct File *f, uint64 addr) {
    // 简化实现：暂不实现完整的 stat 结构
    return 0;
}

// 从文件读取数据
int file_read(struct File *f, uint64 addr, int n) {
    int r = 0;
    
    if(f->readable == 0)
        return -1;
    
    if(f->type == FILE_INODE) {
        ilock(f->ip);
        r = readi(f->ip, 1, addr, f->off, n);
        if(r > 0)
            f->off += r;
        iunlock(f->ip);
        return r;
    }
    
    panic("file_read");
    return -1;
}

// 向文件写入数据
int file_write(struct File *f, uint64 addr, int n) {
    int r, ret = 0;
    
    if(f->writable == 0)
        return -1;
    
    if(f->type == FILE_INODE) {
        // 如果是追加模式，先移动到文件末尾
        if(f->append) {
            ilock(f->ip);
            f->off = f->ip->size;
            iunlock(f->ip);
        }
        
        // 写入可能扩展文件，因此需要分多次提交事务
        int max = ((LOGSIZE - 1 - 1 - 2) / 2) * BSIZE;
        int i = 0;
        while(i < n) {
            int n1 = n - i;
            if(n1 > max)
                n1 = max;
            
            begin_op();
            ilock(f->ip);
            
            // 追加模式每次写入前都要确保在文件末尾
            if(f->append)
                f->off = f->ip->size;
            
            r = writei(f->ip, 1, addr + i, f->off, n1);
            if(r > 0)
                f->off += r;
            iunlock(f->ip);
            end_op();
            
            if(r != n1) {
                break;
            }
            i += r;
        }
        ret = (i == n ? n : -1);
        return ret;
    }
    
    panic("file_write");
    return -1;
}

