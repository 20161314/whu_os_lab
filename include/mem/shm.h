// shm.h

#define MAX_SHM_SEGS 16      // 系统最多支持16个共享内存段
#define MAX_PAGES_PER_SEG 16 // 每个共享段最多包含16个物理页

// 定义共享内存段的结构体
struct shm_segment {
  struct spinlock lock;
  int key;
  uint32 num_pages; // 段包含的页数
  uint64 pa_list[MAX_PAGES_PER_SEG]; // 存储每一页物理地址的数组
  int ref_count;
};

// 全局共享内存表
extern struct shm_table {
  struct spinlock lock;
  struct shm_segment segments[MAX_SHM_SEGS];
} shm_table;

void shm_init(void);
uint64 shm_get(int key, uint32 size);
int shm_spinlock_release(int key);