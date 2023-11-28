/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include "jdi.h"

#include <ctype.h>
#include <fcntl.h> /* fcntl */
#include <pthread.h>
#include <signal.h> /* SIGIO */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h> /* fopen/fread */
#include <sys/ioctl.h> /* fopen/fread */
#include <sys/mman.h>  /* mmap */
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "jpulog.h"
#include "jputypes.h"
#include "list.h"
#include "regdefine.h"

#define JDI_NUM_LOCK_HANDLES 1
#define JDI_SYSTEM_ENDIAN JDI_LITTLE_ENDIAN

#define JPU_DEVICE_NAME "/dev/jpu"
#define JDI_INSTANCE_POOL_SIZE sizeof(jpu_instance_pool_t)
#define JDI_INSTANCE_POOL_TOTAL_SIZE \
  (JDI_INSTANCE_POOL_SIZE + sizeof(pthread_mutex_t) * JDI_NUM_LOCK_HANDLES)

struct jdi_device_zone {
  int zone_node;
  unsigned long base_addr;
  size_t size;
  int low_device_node;
  int high_device_node;
};

typedef struct jpudrv_buffer_pool_t {
  jpudrv_buffer_t jdb;
  BOOL inuse;
} jpudrv_buffer_pool_t;

typedef struct {
  Int32 dev_id;
  Int32 jpu_fd;
  struct list_head dev_list;
  bool initialized;
  jpu_instance_pool_t *pjip;
  Int32 task_num;
  Int32 clock_state;
  jpudrv_buffer_t jdb_register;
  jpudrv_buffer_pool_t jpu_buffer_pool[MAX_JPU_BUFFER_POOL];
  Int32 jpu_buffer_pool_count;
  void *jpu_mutex;
} jdi_info_t;

static pthread_once_t initialized = PTHREAD_ONCE_INIT;
static pthread_mutex_t device_lock;
static struct list_head device_list;

static void jdi_dev_init(void) {
  pthread_mutex_init(&device_lock, NULL);
  INIT_LIST_HEAD(&device_list);
}

int jdi_probe(int dev_id) {
  jdi_info_t *jdi = jdi_init(dev_id);
  if (jdi != NULL) {
    jdi_release(jdi);
    return 0;
  }

  return -1;
}

/* @return number of tasks.
 */
int jdi_get_task_num(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (jdi && jdi->initialized) {
    return jdi->task_num;
  }

  return 0;
}

JdiDeviceCtx jdi_init(int dev_id) {
  jdi_info_t *jdi, *tmp;
  char jdevice_inst_name[128];
  bool opened = false;
  int i;

  // initialized
  pthread_once(&initialized, jdi_dev_init);

  // JPU initialized
  pthread_mutex_lock(&device_lock);

  list_for_each_entry_safe(jdi, tmp, &device_list, dev_list) {
    if (jdi->dev_id == dev_id) {
      opened = true;
      break;
    }
  }

  if (opened) {
    // increase task number
    jdi_lock(jdi);
    jdi->task_num++;
    jdi_unlock(jdi);

    pthread_mutex_unlock(&device_lock);
    return jdi;
  }

  jdi = calloc(1, sizeof(jdi_info_t));
  if (!jdi) {
    pthread_mutex_unlock(&device_lock);
    return NULL;
  }

  jdi->dev_id = dev_id;
  INIT_LIST_HEAD(&jdi->dev_list);

  // open device
  snprintf(jdevice_inst_name, 128, "%s%d", JPU_DEVICE_NAME, dev_id);
  jdi->jpu_fd = open(jdevice_inst_name, O_RDWR);
  if (jdi->jpu_fd < 0) {
    JLOG(ERR, "[JDI] Can't open jpu driver(%s). [error=%s]\n",
         jdevice_inst_name, strerror(errno));
    goto ERR_JDI_INIT;
  }

  if (!jdi_get_instance_pool(jdi)) {
    JLOG(ERR, "[JDI] fail to create instance pool for saving context \n");
    goto ERR_JDI_INIT;
  }

  if (jdi->pjip->instance_pool_inited == FALSE) {
    Uint32 *pCodecInst;
    pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
#if defined(ANDROID) || !defined(PTHREAD_MUTEX_ROBUST_NP)
#else
    /* If a process or a thread is terminated abnormally,
     * pthread_mutexattr_setrobust_np(attr, PTHREAD_MUTEX_ROBUST_NP) makes
     * next onwer call pthread_mutex_lock() without deadlock.
     */

    pthread_mutexattr_setrobust_np(&mutexattr, PTHREAD_MUTEX_ROBUST_NP);
#endif
    pthread_mutex_init((pthread_mutex_t *)jdi->jpu_mutex, &mutexattr);

    for (i = 0; i < MAX_NUM_INSTANCE; i++) {
      pCodecInst = (Uint32 *)jdi->pjip->jpgInstPool[i];
      pCodecInst[1] = i;  // indicate instIndex of CodecInst
      pCodecInst[0] = 0;  // indicate inUse of CodecInst
    }

    jdi->pjip->instance_pool_inited = TRUE;
  }

  if (ioctl(jdi->jpu_fd, JDI_IOCTL_GET_REGISTER_INFO, &jdi->jdb_register) < 0) {
    JLOG(ERR, "[JDI] fail to get host interface register\n");
    goto ERR_JDI_INIT;
  }

  jdi->jdb_register.virt_addr = (unsigned long)mmap /*64*/ (
      NULL, jdi->jdb_register.size, PROT_READ | PROT_WRITE, MAP_SHARED,
      jdi->jpu_fd, /*(off64_t)*/ (jdi->jdb_register.phys_addr));
  if (jdi->jdb_register.virt_addr == (unsigned long)MAP_FAILED) {
    JLOG(ERR, "[JDI] fail to map jpu registers \n");
    goto ERR_JDI_INIT;
  }

  JLOG(DBG, "[JDI] map jdb_register virtaddr=0x%lx, size=%d\n",
       jdi->jdb_register.virt_addr, jdi->jdb_register.size);

  jdi->task_num++;
  list_add_tail(&jdi->dev_list, &device_list);
  jdi->initialized = true;

  // enable JPU clock
  jdi_set_clock_gate(jdi, 1);

  pthread_mutex_unlock(&device_lock);

  JLOG(DBG, "[JDI] initialize jpu device-%d@%p successfully\n", jdi->dev_id,
       jdi);
  return jdi;

ERR_JDI_INIT:
  pthread_mutex_unlock(&device_lock);
  jdi_release(jdi);
  return NULL;
}

int jdi_release(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  pthread_mutex_lock(&device_lock);

  if (!jdi) {
    JLOG(ERR, "%s:%d JDI handle isn't initialized\n", __FUNCTION__, __LINE__);
    pthread_mutex_unlock(&device_lock);
    return 0;
  }
  if (jdi->jpu_fd <= 0) {
    JLOG(ERR, "%s:%d JDI fd isn't initialized\n", __FUNCTION__, __LINE__);
    if (jdi->task_num == 0) {
      JLOG(ERR, "device-%d@%p freed\n", jdi->dev_id, jdi);
      free(jdi);
    }
    pthread_mutex_unlock(&device_lock);
    return 0;
  }
  if (jdi_lock(jdi) < 0) {
    JLOG(ERR, "[JDI] fail to handle lock function\n");
    pthread_mutex_unlock(&device_lock);
    return -1;
  }

  if (jdi->task_num == 0) {
    JLOG(ERR, "[JDI] %s:%d task_num is 0\n", __FUNCTION__, __LINE__);
    jdi_unlock(jdi);
    pthread_mutex_unlock(&device_lock);
    free(jdi);
    return 0;
  }

  jdi->task_num--;
  if (jdi->task_num > 0) {  // means that the opened instance remains
    jdi_unlock(jdi);
    pthread_mutex_unlock(&device_lock);
    return 0;
  }

  if (jdi->jdb_register.virt_addr) {
    if (munmap((void *)jdi->jdb_register.virt_addr, jdi->jdb_register.size) <
        0) {
      JLOG(ERR, "%s:%d failed to munmap\n", __FUNCTION__, __LINE__);
    }
  }

  jdi_unlock(jdi);

  if (jdi_get_clock_gate(devctx)) {
    jdi_set_clock_gate(devctx, 0);
  }

  if (jdi->jpu_fd > 0) {
    if (jdi->pjip != NULL) {
      if (munmap((void *)jdi->pjip, JDI_INSTANCE_POOL_TOTAL_SIZE) < 0) {
        JLOG(ERR, "%s:%d failed to munmap\n", __FUNCTION__, __LINE__);
      }
    }

    close(jdi->jpu_fd);
  }

  if (jdi->initialized) {
    list_del(&jdi->dev_list);
  }

  JLOG(DBG, "device-%d@%p freed\n", jdi->dev_id, jdi);
  free(jdi);

  pthread_mutex_unlock(&device_lock);

  return 0;
}

jpu_instance_pool_t *jdi_get_instance_pool(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  jpudrv_buffer_t jdb;

  if (!jdi || jdi->jpu_fd <= 0) {
    return NULL;
  }

  memset(&jdb, 0x00, sizeof(jpudrv_buffer_t));

  if (!jdi->pjip) {
    jdb.size = JDI_INSTANCE_POOL_TOTAL_SIZE;
    if (ioctl(jdi->jpu_fd, JDI_IOCTL_GET_INSTANCE_POOL, &jdb) < 0) {
      JLOG(ERR, "[JDI] fail to allocate get instance pool physical space=%d\n",
           (int)jdb.size);
      return NULL;
    }

    jdb.virt_addr = (unsigned long)mmap(NULL, jdb.size, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, jdi->jpu_fd, 0);
    if (jdb.virt_addr == (unsigned long)MAP_FAILED) {
      JLOG(ERR, "[JDI] fail to map instance pool phyaddr=0x%lx, size = %d\n",
           (int)jdb.phys_addr, (int)jdb.size);
      return NULL;
    }

    jdi->pjip = (jpu_instance_pool_t *)jdb.virt_addr;
    // change the pointer of jpu_mutex to at end pointer of jpu_instance_pool_t
    // to assign at allocated position.
    jdi->jpu_mutex =
        (void *)((unsigned long)jdi->pjip + JDI_INSTANCE_POOL_SIZE);

    JLOG(DBG,
         "[JDI] instance pool physaddr=%p, virtaddr=%p, base=%p, size=%d\n",
         jdb.phys_addr, jdb.virt_addr, jdb.base, jdb.size);
  }

  return (jpu_instance_pool_t *)jdi->pjip;
}

int jdi_open_instance(JdiDeviceCtx devctx, unsigned long inst_idx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  jpudrv_inst_info_t inst_info;

  if (!jdi || jdi->jpu_fd <= 0) {
    return -1;
  }

  inst_info.inst_idx = inst_idx;

  if (ioctl(jdi->jpu_fd, JDI_IOCTL_OPEN_INSTANCE, &inst_info) < 0) {
    JLOG(ERR, "[JDI] fail to deliver open instance num inst_idx=%d\n",
         (int)inst_idx);
    return -1;
  }

  jdi->pjip->jpu_instance_num = inst_info.inst_open_count;

  return 0;
}

int jdi_close_instance(JdiDeviceCtx devctx, unsigned long inst_idx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  jpudrv_inst_info_t inst_info;

  if (!jdi || jdi->jpu_fd <= 0) return -1;

  inst_info.inst_idx = inst_idx;

  if (ioctl(jdi->jpu_fd, JDI_IOCTL_CLOSE_INSTANCE, &inst_info) < 0) {
    JLOG(ERR, "[JDI] fail to deliver open instance num inst_idx=%d\n",
         (int)inst_idx);
    return -1;
  }

  jdi->pjip->jpu_instance_num = inst_info.inst_open_count;

  return 0;
}

int jdi_get_instance_num(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (!jdi || !jdi->initialized) {
    return -1;
  }

  return jdi->pjip->jpu_instance_num;
}

int jdi_hw_reset(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (!jdi || jdi->jpu_fd <= 0) {
    return -1;
  }

  return ioctl(jdi->jpu_fd, JDI_IOCTL_RESET, 0);
}

static void restore_mutex_in_dead(pthread_mutex_t *mutex) {
  int mutex_value;

  if (!mutex) {
    return;
  }

  memcpy(&mutex_value, mutex, sizeof(mutex_value));

  if (mutex_value == (int)0xdead10cc) {  // destroy by device driver
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mutex, &mutexattr);
  }
}

int jdi_lock(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (!jdi || jdi->jpu_fd <= 0) {
    JLOG(ERR, "%s:%d JDI handle isn't initialized\n", __FUNCTION__, __LINE__);
    return -1;
  }

  if (pthread_mutex_lock((pthread_mutex_t *)jdi->jpu_mutex) != 0) {
    JLOG(ERR, "%s:%d failed to pthread_mutex_locK\n", __FUNCTION__, __LINE__);
    return -1;
  }

  return 0;
}

void jdi_unlock(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (!jdi || jdi->jpu_fd <= 0) {
    return;
  }

  pthread_mutex_unlock((pthread_mutex_t *)jdi->jpu_mutex);
}

void jdi_write_register(JdiDeviceCtx devctx, unsigned long addr,
                        unsigned int data) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  unsigned long *reg_addr;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return;
  }

  reg_addr =
      (unsigned long *)(addr + (unsigned long)jdi->jdb_register.virt_addr);
  JLOG(TRACE, "jdi write register %lx/%x\n", reg_addr, data);
  *(volatile unsigned int *)reg_addr = data;
}

unsigned long jdi_read_register(JdiDeviceCtx devctx, unsigned long addr) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  unsigned long *reg_addr;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return (unsigned int)-1;
  }

  reg_addr =
      (unsigned long *)(addr + (unsigned long)jdi->jdb_register.virt_addr);
  JLOG(TRACE, "jdi read register %lx/%x\n", reg_addr,
       *(volatile unsigned int *)reg_addr);
  return *(volatile unsigned int *)reg_addr;
}
#if 0
int jdi_write_memory(JdiDeviceCtx devctx, unsigned char *addr, unsigned char *data, int len, int endian)
{
    jdi_info_t *jdi = (jdi_info_t *)devctx;

    if(!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
        return -1;
    }
    swap_endian(data, len, endian);
    memcpy((void *)(addr), data, len);

    JLOG(DBG, "jdi write memory at %p/%d, src %p\n", addr,len, data);

    return len;
}

int jdi_read_memory(JdiDeviceCtx devctx, unsigned char *addr, unsigned char *data, int len, int endian)
{
    jdi_info_t *jdi = (jdi_info_t *)devctx;

    if(!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
        return -1;
    }

    JLOG(DBG, "jdi read memory at %p/%d, dst %p\n",addr, len, data);
    memcpy(data, addr, len);
    swap_endian(data, len,  endian);
    return len;
}
#endif
int jdi_allocate_dma_memory(JdiDeviceCtx devctx, jpu_buffer_t *vb) {
#if 0
    jdi_info_t *jdi = (jdi_info_t *)devctx;

    if(!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
        return -1;
    }

    memset(&jdb, 0x00, sizeof(jpudrv_buffer_t));

    jdb.size = vb->size;

    if (ioctl(jdi->jpu_fd, JDI_IOCTL_ALLOCATE_PHYSICAL_MEMORY, &jdb) < 0) {
        JLOG(ERR, "[JDI] fail to jdi_allocate_dma_memory size=%d\n", vb->size);
        return -1;
    }

    vb->phys_addr = (unsigned long)jdb.phys_addr;
    vb->base = (unsigned long)jdb.base;

    //map to virtual address
    jdb.virt_addr = (unsigned long)mmap(NULL, jdb.size, PROT_READ | PROT_WRITE, MAP_SHARED, jdi->jpu_fd, jdb.phys_addr);
    if (jdb.virt_addr == (unsigned long)MAP_FAILED) {
        memset(vb, 0x00, sizeof(jpu_buffer_t));
        return -1;
    }

    vb->virt_addr = jdb.virt_addr;

    jdi_lock(jdi);

    for (i = 0; i < MAX_JPU_BUFFER_POOL; i++) {
        if (jdi->jpu_buffer_pool[i].inuse == 0) {
            jdi->jpu_buffer_pool[i].jdb = jdb;
            jdi->jpu_buffer_pool_count++;
            jdi->jpu_buffer_pool[i].inuse = 1;
            break;
        }
    }

    jdi_unlock(jdi);

    JLOG(DBG, "[JDI] jdi_allocate_dma_memory, physaddr=%lx, virtaddr=%lx~%lx, size=%d\n",
         vb->phys_addr, vb->virt_addr, vb->virt_addr + vb->size, vb->size);
#endif
  return 0;
}

void jdi_free_dma_memory(JdiDeviceCtx devctx, jpu_buffer_t *vb) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  int i;
  jpudrv_buffer_t jdb;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return;
  }

  if (vb->size == 0) {
    return;
  }

  memset(&jdb, 0x00, sizeof(jpudrv_buffer_t));

  jdi_lock(jdi);

  for (i = 0; i < MAX_JPU_BUFFER_POOL; i++) {
    if (jdi->jpu_buffer_pool[i].jdb.phys_addr == vb->phys_addr) {
      jdi->jpu_buffer_pool[i].inuse = 0;
      jdi->jpu_buffer_pool_count--;
      jdb = jdi->jpu_buffer_pool[i].jdb;
      break;
    }
  }

  jdi_unlock(jdi);

  if (!jdb.size) {
    JLOG(ERR, "[JDI] invalid buffer to free address = 0x%lx\n",
         (int)jdb.virt_addr);
    return;
  }

  ioctl(jdi->jpu_fd, JDI_IOCTL_FREE_PHYSICALMEMORY, &jdb);

  if (munmap((void *)jdb.virt_addr, jdb.size) != 0) {
    JLOG(ERR, "[JDI] fail to jdi_free_dma_memory virtial address = 0x%lx\n",
         (int)jdb.virt_addr);
  }

  memset(vb, 0, sizeof(jpu_buffer_t));
}

int jdi_set_clock_gate(JdiDeviceCtx devctx, int enable) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  int ret;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return -1;
  }

  jdi->clock_state = enable;
  ret = ioctl(jdi->jpu_fd, JDI_IOCTL_SET_CLOCK_GATE, &enable);

  return ret;
}

int jdi_get_clock_gate(JdiDeviceCtx devctx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return -1;
  }

  return jdi->clock_state;
}

int jdi_wait_inst_ctrl_busy(JdiDeviceCtx devctx, int timeout,
                            unsigned int addr_flag_reg, unsigned int flag) {
  unsigned int data_flag_reg;

  while (1) {
    data_flag_reg = jdi_read_register(devctx, addr_flag_reg);
    if (((data_flag_reg >> 4) & 0xf) == flag) {
      break;
    }
  }

  return 0;
}

int jdi_wait_interrupt(JdiDeviceCtx devctx, int timeout,
                       unsigned int addr_int_reason, unsigned long instIdx) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  int intr_reason = 0;
  int ret;
  jpudrv_intr_info_t intr_info;

  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return -1;
  }

  intr_info.timeout = timeout;
  intr_info.intr_reason = 0;
  intr_info.inst_idx = instIdx;
  ret = ioctl(jdi->jpu_fd, JDI_IOCTL_WAIT_INTERRUPT, (void *)&intr_info);
  if (ret != 0) {
    return -1;
  }

  intr_reason = intr_info.intr_reason;

  return intr_reason;
}

JPU_DMA_CFG jdi_config_mmu(JdiDeviceCtx devctx, int input_buffer_fd,
                           int output_buffer_fd, unsigned int data_size,
                           unsigned int append_size) {
  jdi_info_t *jdi = (jdi_info_t *)devctx;
  int ret;
  JPU_DMA_CFG cfg;
  memset((void *)&cfg, 0x00, sizeof(JPU_DMA_CFG));
  if (!jdi || !jdi->initialized || jdi->jpu_fd <= 0) {
    return cfg;
  }
  cfg.intput_buf_fd = input_buffer_fd;
  cfg.output_buf_fd = output_buffer_fd;
  cfg.data_size = data_size;
  cfg.append_buf_size = append_size;

  ret = ioctl(jdi->jpu_fd, JDI_IOCTL_CFG_MMU, (void *)&cfg);
  if (ret != 0) {
    return cfg;
  }
  return cfg;
}

void jdi_log(int cmd, int step, int inst) { return; }

int jdi_set_clock_freg(int Device, int OutFreqMHz, int InFreqMHz) { return 0; }
