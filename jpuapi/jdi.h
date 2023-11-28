/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef _JDI_HPI_H_
#define _JDI_HPI_H_
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpu.h"
#include "jpuconfig.h"
#include "jputypes.h"
#include "mm.h"

#define MAX_JPU_BUFFER_POOL 4096

typedef struct jpu_instance_pool_t {
  unsigned char jpgInstPool[MAX_NUM_INSTANCE][MAX_INST_HANDLE_SIZE];
  Int32 jpu_instance_num;
  BOOL instance_pool_inited;
  void *instPendingInst[MAX_NUM_INSTANCE];
  jpeg_mm_t vmem;
} jpu_instance_pool_t;

typedef struct jpu_buffer_t {
  unsigned int size;
  unsigned long phys_addr;
  unsigned long base;
  void *virt_addr;
  unsigned int fd;
} jpu_buffer_t;

typedef enum {
  JDI_LOG_CMD_PICRUN = 0,
  JDI_LOG_CMD_INIT = 1,
  JDI_LOG_CMD_RESET = 2,
  JDI_LOG_CMD_PAUSE_INST_CTRL = 3,
  JDI_LOG_CMD_MAX
} jdi_log_cmd;

#if defined(__cplusplus)
extern "C" {
#endif
int jdi_probe(int dev_id);
/* @brief It returns the number of task using JDI.
 */
int jdi_get_task_num(JdiDeviceCtx devctx);
JdiDeviceCtx jdi_init(int dev_id);
int jdi_release(
    JdiDeviceCtx devctx);  // this function may be called only at system off.
jpu_instance_pool_t *jdi_get_instance_pool(JdiDeviceCtx devctx);
int jdi_allocate_dma_memory(JdiDeviceCtx devctx, jpu_buffer_t *vb);
void jdi_free_dma_memory(JdiDeviceCtx devctx, jpu_buffer_t *vb);

int jdi_wait_interrupt(JdiDeviceCtx devctx, int timeout,
                       unsigned int addr_int_reason, unsigned long instIdx);
int jdi_hw_reset(JdiDeviceCtx devctx);
int jdi_wait_inst_ctrl_busy(JdiDeviceCtx devctx, int timeout,
                            unsigned int addr_flag_reg, unsigned int flag);
JPU_DMA_CFG jdi_config_mmu(JdiDeviceCtx devctx, int input_buffer_fd,
                           int output_buffer_fd, unsigned int dataSize,
                           unsigned int appendingSize);
int jdi_set_clock_gate(JdiDeviceCtx devctx, int enable);
int jdi_get_clock_gate(JdiDeviceCtx devctx);

int jdi_open_instance(JdiDeviceCtx devctx, unsigned long instIdx);
int jdi_close_instance(JdiDeviceCtx devctx, unsigned long instIdx);
int jdi_get_instance_num(JdiDeviceCtx devctx);

#if 1
void jdi_write_register(JdiDeviceCtx devctx, unsigned long addr,
                        unsigned int data);
unsigned long jdi_read_register(JdiDeviceCtx devctx, unsigned long addr);
#endif
int jdi_lock(JdiDeviceCtx devctx);
void jdi_unlock(JdiDeviceCtx devctx);
void jdi_log(int cmd, int step, int inst);

#define ACLK_MAX 300
#define ACLK_MIN 16
#define CCLK_MAX 300
#define CCLK_MIN 16

#if defined(__cplusplus)
}
#endif

#endif  //#ifndef _JDI_HPI_H_
