/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef __JPU_DRV_H__
#define __JPU_DRV_H__

#include <linux/fs.h>
#include <linux/types.h>

#define JDI_IOCTL_MAGIC 'J'

#define JDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY _IO(JDI_IOCTL_MAGIC, 0)
#define JDI_IOCTL_FREE_PHYSICALMEMORY _IO(JDI_IOCTL_MAGIC, 1)
#define JDI_IOCTL_WAIT_INTERRUPT _IO(JDI_IOCTL_MAGIC, 2)
#define JDI_IOCTL_SET_CLOCK_GATE _IO(JDI_IOCTL_MAGIC, 3)
#define JDI_IOCTL_RESET _IO(JDI_IOCTL_MAGIC, 4)
#define JDI_IOCTL_GET_INSTANCE_POOL _IO(JDI_IOCTL_MAGIC, 5)
#define JDI_IOCTL_GET_RESERVED_VIDEO_MEMORY_INFO _IO(JDI_IOCTL_MAGIC, 6)
#define JDI_IOCTL_GET_REGISTER_INFO _IO(JDI_IOCTL_MAGIC, 7)
#define JDI_IOCTL_OPEN_INSTANCE _IO(JDI_IOCTL_MAGIC, 8)
#define JDI_IOCTL_CLOSE_INSTANCE _IO(JDI_IOCTL_MAGIC, 9)
#define JDI_IOCTL_GET_INSTANCE_NUM _IO(JDI_IOCTL_MAGIC, 10)
#define JDI_IOCTL_CFG_MMU _IO(JDI_IOCTL_MAGIC, 11)

typedef struct jpudrv_buffer_t {
  unsigned int size;
  unsigned long phys_addr;
  unsigned long base;      /* kernel logical address in use kernel */
  unsigned long virt_addr; /* virtual user space address */
} jpudrv_buffer_t;

typedef struct jpudrv_inst_info_t {
  unsigned int inst_idx;
  int inst_open_count; /* for output only*/
} jpudrv_inst_info_t;

typedef struct jpudrv_intr_info_t {
  unsigned int timeout;
  int intr_reason;
  unsigned int inst_idx;
} jpudrv_intr_info_t;
typedef struct jpu_dma_cfg {
  int intput_buf_fd;
  int output_buf_fd;
  unsigned int intput_virt_addr;
  unsigned int output_virt_addr;
  unsigned int data_size;
  unsigned int append_buf_size;
} JPU_DMA_CFG;

#endif
