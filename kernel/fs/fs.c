#include "common.h"
#include "fs/bio.h"
#include "fs/log.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "dev/console.h"
#include "lib/str.h"

// 超级块
struct superblock sb;

// inode 缓存
#define NINODE 100
struct {
    struct inode inode[NINODE];
} icache;

// 初始化文件系统
void fs_init(int dev) {
    struct buf *bp;
    
    //printf("[fs_init] Starting, dev=%d\n", (int)dev);
    bio_init();
    //printf("[fs_init] bio_init done\n");
    
    // 读取超级块
    bp = bread(dev, 1);
    //printf("[fs_init] bread(dev=1) done\n");
    memmove(&sb, bp->data, sizeof(sb));
    brelse(bp);
    //printf("[fs_init] superblock read, magic=0x%x\n", (int)sb.magic);
    
    // 如果超级块魔数不匹配，创建新的文件系统
    if(sb.magic != FSMAGIC) {
        printf("fs_init: creating new filesystem\n");
        
        // 初始化超级块
        sb.magic = FSMAGIC;
        sb.size = FSSIZE;
        sb.nblocks = FSSIZE - 100; // 预留一些空间给元数据
        sb.ninodes = 200;
        sb.nlog = 30;
        sb.logstart = 2;
        sb.inodestart = 2 + sb.nlog;
        sb.bmapstart = sb.inodestart + sb.ninodes / IPB + 1;
        
        // 写入超级块
        bp = bread(dev, 1);
        memmove(bp->data, &sb, sizeof(sb));
        bwrite(bp);
        brelse(bp);
        
        printf("fs_init: superblock written\n");
        printf("  size=%d nblocks=%d ninodes=%d\n", 
               (int)sb.size, (int)sb.nblocks, (int)sb.ninodes);
        
        //printf("[fs_init] Calling log_init...\n");
        log_init(dev, &sb);
        //printf("[fs_init] log_init done\n");
        
        // 初始化位图,标记所有系统块为已使用
        // 系统块: 引导块(0)、超级块(1)、日志(2-31)、inode(32-57)、位图(58)
        //printf("fs_init: marking system blocks 0-%d as used\n", (int)sb.bmapstart);
        
        //printf("[fs_init] Calling begin_op...\n");
        begin_op();
        //printf("[fs_init] begin_op done\n");
        // 第一个位图块在 sb.bmapstart
        bp = bread(dev, sb.bmapstart);
        // 标记块 0 到 sb.bmapstart
        for(int b = 0; b <= sb.bmapstart; b++) {
            int bi = b % BPB;  // 在位图中的位索引
            int m = 1 << (bi % 8);
            bp->data[bi / 8] |= m;
        }
        log_write(bp);
        brelse(bp);
        end_op();
        
        // 创建根目录
        struct inode *root;
        begin_op();
        root = ialloc(dev, T_DIR);
        if(root->inum != ROOTINO)
            panic("fs_init: root inode != ROOTINO");
        ilock(root);
        root->nlink = 2;  // 至少需要 2 个链接：自己和 "."
        root->size = 0;
        iupdate(root);
        
        // 添加 . 和 .. 目录项
        dirlink(root, ".", ROOTINO);
        dirlink(root, "..", ROOTINO);
        iunlockput(root);
        end_op();
        
        //printf("fs_init: root directory created\n");
    } else {
        log_init(dev, &sb);
    }
}

// 分配磁盘块
int fs_alloc(uint64 dev) {
    struct buf *bp;
    int b, bi, m;
    
    bp = 0;
    for(b = 0; b < sb.size; b += BPB) {
        bp = bread(dev, BBLOCK(b, sb));
        for(bi = 0; bi < BPB && b + bi < sb.size; bi++) {
            m = 1 << (bi % 8);
            if((bp->data[bi / 8] & m) == 0) {  // 块空闲
                bp->data[bi / 8] |= m;  // 标记为已使用
                log_write(bp);
                brelse(bp);
                
                // 清零块内容
                struct buf *zbp = bread(dev, b + bi);
                memset(zbp->data, 0, BSIZE);
                log_write(zbp);
                brelse(zbp);
                
                return b + bi;
            }
        }
        brelse(bp);
    }
    
    panic("fs_alloc: out of blocks");
    return 0;
}

// 释放磁盘块
void fs_free(int dev, uint64 b) {
    struct buf *bp;
    int bi, m;
    
    bp = bread(dev, BBLOCK(b, sb));
    bi = b % BPB;
    m = 1 << (bi % 8);
    
    if((bp->data[bi / 8] & m) == 0)
        panic("fs_free: block not in use");
    
    bp->data[bi / 8] &= ~m;
    log_write(bp);
    brelse(bp);
}

// 分配一个新的 inode
struct inode* ialloc(uint64 dev, short type) {
    int inum;
    struct buf *bp;
    struct dinode *dip;
    struct inode *ip;
    
    for(inum = 1; inum < sb.ninodes; inum++) {
        bp = bread(dev, IBLOCK(inum, sb));
        dip = (struct dinode*)bp->data + inum % IPB;
        
        if(dip->type == 0) {  // 空闲 inode
            memset((addr_t)dip, 0, sizeof(*dip));
            dip->type = type;
            log_write(bp);
            brelse(bp);
            
            // 获取 inode 并立即初始化其内存表示
            ip = iget(dev, inum);
            ip->type = type;
            ip->major = 0;
            ip->minor = 0;
            ip->nlink = 0;
            ip->size = 0;
            memset((addr_t)ip->addrs, 0, sizeof(ip->addrs));
            ip->valid = 1;  // 标记为有效，避免从磁盘重新加载
            
            return ip;
        }
        brelse(bp);
    }
    
    panic("ialloc: no inodes");
    return 0;
}

// 获取 inode（从缓存或磁盘）
struct inode* iget(uint64 dev, uint64 inum) {
    struct inode *ip, *empty;
    
    empty = 0;
    
    // 在缓存中查找
    for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
        if(ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            return ip;
        }
        if(empty == 0 && ip->ref == 0)
            empty = ip;
    }
    
    // 未找到，分配新的缓存项
    if(empty == 0) {
        // 尝试回收一个未使用的 inode
        for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {
            if(ip->ref == 0) {
                empty = ip;
                break;
            }
        }
        if(empty == 0)
            panic("iget: no inodes");
    }
    
    ip = empty;
    // 清空旧数据，但不设置 type，让 ilock 从磁盘加载
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0;
    
    return ip;
}

// 增加 inode 引用计数
struct inode* idup(struct inode *ip) {
    ip->ref++;
    return ip;
}

// 锁定 inode 并从磁盘加载
void ilock(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;
    
    if(ip == 0 || ip->ref < 1)
        panic("ilock");
    
    if(ip->valid == 0) {
        bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        dip = (struct dinode*)bp->data + ip->inum % IPB;
        
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        
        brelse(bp);
        ip->valid = 1;
        
        if(ip->type == 0) {
            printf("ilock: no type for inum=%d, dev=%d\n", (int)ip->inum, (int)ip->dev);
            panic("ilock: no type");
        }
    }
}

// 解锁 inode
void iunlock(struct inode *ip) {
    if(ip == 0 || ip->ref < 1)
        panic("iunlock");
}

// 更新 inode 到磁盘
void iupdate(struct inode *ip) {
    struct buf *bp;
    struct dinode *dip;
    
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum % IPB;
    
    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
    
    log_write(bp);
    brelse(bp);
}

// 释放 inode
void iput(struct inode *ip) {
    // 保护根目录，永远不释放
    if(ip->inum == ROOTINO && ip->ref == 1) {
        ip->ref--;
        return;
    }
    
    if(ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // inode 无链接，释放它
        itrunc(ip);
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;
    }
    
    ip->ref--;
}

// 解锁并释放 inode
void iunlockput(struct inode *ip) {
    iunlock(ip);
    iput(ip);
}

// 将文件偏移映射到磁盘块
static uint64 bmap(struct inode *ip, uint64 bn) {
    uint32 addr;
    uint32 *a;
    struct buf *bp;
    
    if(bn < NDIRECT) {
        if((addr = ip->addrs[bn]) == 0)
            ip->addrs[bn] = addr = fs_alloc(ip->dev);
        return addr;
    }
    bn -= NDIRECT;
    
    if(bn < NINDIRECT) {
        // 间接块
        if((addr = ip->addrs[NDIRECT]) == 0)
            ip->addrs[NDIRECT] = addr = fs_alloc(ip->dev);
        bp = bread(ip->dev, addr);
        a = (uint32*)bp->data;
        if((addr = a[bn]) == 0) {
            a[bn] = addr = fs_alloc(ip->dev);
            log_write(bp);
        }
        brelse(bp);
        return addr;
    }
    
    panic("bmap: out of range");
    return 0;
}

// 截断 inode（释放所有数据块）
void itrunc(struct inode *ip) {
    int i, j;
    struct buf *bp;
    uint32 *a;
    
    for(i = 0; i < NDIRECT; i++) {
        if(ip->addrs[i]) {
            fs_free(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }
    
    if(ip->addrs[NDIRECT]) {
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint32*)bp->data;
        for(j = 0; j < NINDIRECT; j++) {
            if(a[j])
                fs_free(ip->dev, a[j]);
        }
        brelse(bp);
        fs_free(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }
    
    ip->size = 0;
    iupdate(ip);
}

// 从 inode 读取数据
int readi(struct inode *ip, int user_dst, uint64 dst, uint64 off, uint64 n) {
    uint64 tot, m;
    struct buf *bp;
    
    if(off > ip->size || off + n < off)
        return 0;
    if(off + n > ip->size)
        n = ip->size - off;
    
    for(tot = 0; tot < n; tot += m, off += m, dst += m) {
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = BSIZE - off % BSIZE;
        if(n - tot < m)
            m = n - tot;
        
        if(user_dst) {
            // 复制到用户空间
            if(uvm_copyout(myproc()->pgtbl, dst, (uint64)(bp->data + off % BSIZE), m) < 0) {
                brelse(bp);
                return -1;
            }
        } else {
            memmove((void*)dst, bp->data + off % BSIZE, m);
        }
        
        brelse(bp);
    }
    
    return n;
}

// 向 inode 写入数据
int writei(struct inode *ip, int user_src, uint64 src, uint64 off, uint64 n) {
    uint64 tot, m;
    struct buf *bp;
    
    if(off > ip->size || off + n < off)
        return -1;
    if(off + n > MAXFILE * BSIZE)
        return -1;
    
    for(tot = 0; tot < n; tot += m, off += m, src += m) {
        bp = bread(ip->dev, bmap(ip, off / BSIZE));
        m = BSIZE - off % BSIZE;
        if(n - tot < m)
            m = n - tot;
        
        if(user_src) {
            // 从用户空间复制
            if(uvm_copyin(myproc()->pgtbl, (uint64)(bp->data + off % BSIZE), src, m) < 0) {
                brelse(bp);
                return -1;
            }
        } else {
            memmove(bp->data + off % BSIZE, (void*)src, m);
        }
        
        log_write(bp);
        brelse(bp);
    }
    
    if(off > ip->size)
        ip->size = off;
    
    iupdate(ip);
    
    return n;
}

// 在目录中查找文件
struct inode* dirlookup(struct inode *dp, char *name, uint64 *poff) {
    uint64 off;
    struct dirent de;
    
    if(dp->type != T_DIR)
        panic("dirlookup: not DIR");
    
    for(off = 0; off < dp->size; off += sizeof(de)) {
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlookup: read");
        if(de.inum == 0)
            continue;
        if(strncmp(name, de.name, DIRSIZ) == 0) {
            if(poff)
                *poff = off;
            return iget(dp->dev, de.inum);
        }
    }
    
    return 0;
}

// 在目录中添加目录项
int dirlink(struct inode *dp, char *name, uint64 inum) {
    int off;
    struct dirent de;
    struct inode *ip;
    
    // 检查名称是否已存在
    if((ip = dirlookup(dp, name, 0)) != 0) {
        iput(ip);
        return -1;
    }
    
    // 查找空闲目录项
    for(off = 0; off < dp->size; off += sizeof(de)) {
        if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
            panic("dirlink: read");
        if(de.inum == 0)
            break;
    }
    
    strncpy(de.name, name, DIRSIZ);
    de.inum = inum;
    
    if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
        panic("dirlink: write");
    
    return 0;
}

// 跳过路径分隔符
static char* skipelem(char *path, char *name) {
    char *s;
    int len;
    
    while(*path == '/')
        path++;
    if(*path == 0)
        return 0;
    s = path;
    while(*path != '/' && *path != 0)
        path++;
    len = path - s;
    if(len >= DIRSIZ)
        len = DIRSIZ - 1;
    memmove(name, s, len);
    name[len] = 0;
    while(*path == '/')
        path++;
    return path;
}

// 路径解析通用函数
static struct inode* namex(char *path, int nameiparent, char *name) {
    struct inode *ip, *next;
    
    if(*path == '/')
        ip = iget(ROOTDEV, ROOTINO);
    else
        ip = idup(iget(ROOTDEV, ROOTINO));  // 简化：当前目录就是根目录
    
    while((path = skipelem(path, name)) != 0) {
        ilock(ip);
        if(ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }
        if(nameiparent && *path == '\0') {
            iunlock(ip);
            return ip;
        }
        if((next = dirlookup(ip, name, 0)) == 0) {
            iunlockput(ip);
            return 0;
        }
        iunlockput(ip);
        ip = next;
    }
    if(nameiparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

// 查找路径对应的 inode
struct inode* namei(char *path) {
    char name[DIRSIZ];
    return namex(path, 0, name);
}

// 查找路径的父目录 inode
struct inode* nameiparent(char *path, char *name) {
    return namex(path, 1, name);
}

// 将inode信息复制到stat结构
void stati(struct inode *ip, struct stat *st) {
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
}
