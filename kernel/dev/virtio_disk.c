//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "memlayout.h"
#include "common.h"
#include "riscv.h"
#include "fs/fs.h"
#include "fs/bio.h"
#include "fs/file.h"
#include "mem/pmem.h"
#include "dev/virtio.h"
#include "dev/console.h"
#include "lib/str.h"

#define R(r) ((volatile uint32 *)(VIRTIO_BASE + (r)))
#define VRING_ALIGN 16

static struct {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;

  char free[NUM];
  uint16 used_idx;

  struct {
    struct buf *b;
    char status;
  } info[NUM];

  struct virtio_blk_req ops[NUM];
} disk;

static int alloc_desc(void);
static void free_desc(int i);
static void free_chain(int i);
static int alloc3_desc(int *idx);
static void virtio_disk_process_used(void);

void
virtio_disk_init(void)
{
  uint32 status = 0;

  uint32 magic = *R(VIRTIO_MMIO_MAGIC_VALUE);
  uint32 version = *R(VIRTIO_MMIO_VERSION);
  uint32 device_id = *R(VIRTIO_MMIO_DEVICE_ID);
  uint32 vendor_id = *R(VIRTIO_MMIO_VENDOR_ID);

  if(magic != 0x74726976 || (version != 1 && version != 2) || device_id != 2 || vendor_id != 0x554d4551){
    printf("virtio mmio probe failed: magic=0x%x version=0x%x dev=0x%x vendor=0x%x\n",
           magic, version, device_id, vendor_id);
    panic("could not find virtio disk");
  }

  *R(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1ULL << VIRTIO_BLK_F_RO);
  features &= ~(1ULL << VIRTIO_BLK_F_SCSI);
  features &= ~(1ULL << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1ULL << VIRTIO_BLK_F_MQ);
  features &= ~(1ULL << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  status = *R(VIRTIO_MMIO_STATUS);
  if((status & VIRTIO_CONFIG_S_FEATURES_OK) == 0)
    panic("virtio disk FEATURES_OK unset");

  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  char *queue_mem = (char *)pmem_alloc();
  if(queue_mem == 0)
    panic("virtio disk pmem_alloc");

  memset((addr_t)queue_mem, 0, PGSIZE);

  disk.desc = (struct virtq_desc *)queue_mem;
  disk.avail = (struct virtq_avail *)(queue_mem + NUM * sizeof(struct virtq_desc));
  char *avail_end = (char *)disk.avail + sizeof(struct virtq_avail);
  char *used_addr = (char *)(((uint64)avail_end + VRING_ALIGN - 1) & ~(uint64)(VRING_ALIGN - 1));
  disk.used = (struct virtq_used *)used_addr;

  if(used_addr + sizeof(struct virtq_used) > queue_mem + PGSIZE)
    panic("virtio ring too large");

  *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;
  *R(VIRTIO_MMIO_QUEUE_ALIGN) = VRING_ALIGN;
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)queue_mem) >> 12;
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;
}

static int
alloc_desc(void)
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
}

static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

static void
virtio_disk_process_used(void)
{
  __sync_synchronize();
  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk status");

    struct buf *b = disk.info[id].b;
    if(b)
      b->disk = 0;

    disk.used_idx += 1;
  }
}

void
virtio_disk_rw(struct buf *b, int write)
{
  uint64 sector = b->blockno * (BSIZE / 512);

  int idx[3];
  while(alloc3_desc(idx) != 0)
    virtio_disk_process_used();

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  buf0->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  disk.desc[idx[1]].flags = write ? 0 : VRING_DESC_F_WRITE;
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff;
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
  disk.desc[idx[2]].next = 0;

  b->disk = 1;
  disk.info[idx[0]].b = b;

  disk.avail->ring[disk.avail->idx % NUM] = idx[0];
  __sync_synchronize();
  disk.avail->idx += 1;
  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while(b->disk == 1)
    virtio_disk_process_used();

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);
}

void
virtio_disk_intr(void)
{
  uint32 irq_status = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
  if(irq_status)
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = irq_status;

  virtio_disk_process_used();
}
