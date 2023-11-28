/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include "jpuapi.h"

#include <linux/dma-buf.h>
#include <sys/mman.h>

#include "jpuapifunc.h"
#include "jpulog.h"
#include "jputable.h"
#include "regdefine.h"

static JPUCap g_JpuAttributes;
static void SwapByte(Uint8 *data, Uint32 len) {
  Uint8 temp;
  Uint32 i;

  for (i = 0; i < len; i += 2) {
    temp = data[i];
    data[i] = data[i + 1];
    data[i + 1] = temp;
  }
}

static void SwapWord(Uint8 *data, Uint32 len) {
  Uint16 temp;
  Uint16 *ptr = (Uint16 *)data;
  Int32 i, size = len / sizeof(Uint16);

  for (i = 0; i < size; i += 2) {
    temp = ptr[i];
    ptr[i] = ptr[i + 1];
    ptr[i + 1] = temp;
  }
}

static void SwapDword(Uint8 *data, Uint32 len) {
  Uint32 temp;
  Uint32 *ptr = (Uint32 *)data;
  Int32 i, size = len / sizeof(Uint32);

  for (i = 0; i < size; i += 2) {
    temp = ptr[i];
    ptr[i] = ptr[i + 1];
    ptr[i + 1] = temp;
  }
}

static Int32 swap_endian(BYTE *data, Uint32 len, Uint32 endian) {
  Uint8 endianMask[8] = {
      // endianMask : [2] - 4byte unit swap
      0x00, 0x07, 0x04, 0x03,  //              [1] - 2byte unit swap
      0x06, 0x05, 0x02, 0x01   //              [0] - 1byte unit swap
  };
  Uint8 targetEndian;
  Uint8 systemEndian;
  Uint8 changes;
  BOOL byteSwap = FALSE, wordSwap = FALSE, dwordSwap = FALSE;

  if (endian > 7) {
    JLOG(ERR, "Invalid endian mode: %d, expected value: 0~7\n", endian);
    return -1;
  }

  targetEndian = endianMask[endian];
  systemEndian = endianMask[JDI_LITTLE_ENDIAN];
  changes = targetEndian ^ systemEndian;
  byteSwap = changes & 0x01 ? TRUE : FALSE;
  wordSwap = changes & 0x02 ? TRUE : FALSE;
  dwordSwap = changes & 0x04 ? TRUE : FALSE;

  if (byteSwap == TRUE) SwapByte(data, len);
  if (wordSwap == TRUE) SwapWord(data, len);
  if (dwordSwap == TRUE) SwapDword(data, len);

  return changes == 0 ? 0 : 1;
}

int jdi_write_memory(JdiDeviceCtx devctx, unsigned char *addr,
                     unsigned char *data, int len, int endian) {
  swap_endian(data, len, endian);
  memcpy((void *)(addr), data, len);

  JLOG(DBG, "jdi write memory at %p/%d, src %p\n", addr, len, data);

  return len;
}

int jdi_read_memory(JdiDeviceCtx devctx, unsigned char *addr,
                    unsigned char *data, int len, int endian) {
  JLOG(DBG, "jdi read memory at %p/%d, dst %p\n", addr, len, data);
  memcpy(data, addr, len);
  swap_endian(data, len, endian);
  return len;
}

int JPU_IsBusy(JpgHandle handle) {
  Uint32 val;
  JpgInst *pJpgInst = (JpgInst *)handle;
  Int32 instRegIndex;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }
  val = JpuReadInstReg(handle->devctx, instRegIndex, MJPEG_PIC_STATUS_REG);

  if ((val & (1 << INT_JPU_DONE)) || (val & (1 << INT_JPU_ERROR))) return 0;

  return 1;
}

void JPU_ClrStatus(JpgHandle handle, Uint32 val) {
  JpgInst *pJpgInst = (JpgInst *)handle;
  Int32 instRegIndex;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }
  if (val != 0)
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_STATUS_REG, val);
}
Uint32 JPU_GetStatus(JpgHandle handle) {
  JpgInst *pJpgInst = (JpgInst *)handle;
  Int32 instRegIndex;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  return JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_STATUS_REG);
}

Uint32 JPU_IsInit(JdiDeviceCtx devctx) {
  jpu_instance_pool_t *pjip;

  pjip = (jpu_instance_pool_t *)jdi_get_instance_pool(devctx);

  if (!pjip) return 0;

  return 1;
}

int JPU_ShowRegisters(JpgHandle handle) {
  JpgInst *pJpgInst = (JpgInst *)handle;
  Int32 instRegIndex;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

#define show_register(reg)                                                 \
  do {                                                                     \
    JLOG(ERR, "device %p: register %s, %04x@%08x\n", handle->devctx, #reg, \
         reg, JpuReadInstReg(handle->devctx, instRegIndex, reg));          \
  } while (0)

  JLOG(ERR, ">>>>>>>>>>>>>>>> JPU registers <<<<<<<<<<<<<<<<\n");
  show_register(MJPEG_INTR_MASK_REG);
  show_register(MJPEG_SLICE_INFO_REG);
  show_register(MJPEG_SLICE_DPB_POS_REG);
  show_register(MJPEG_SLICE_POS_REG);
  show_register(MJPEG_PIC_SETMB_REG);
  show_register(MJPEG_CLP_INFO_REG);
  show_register(MJPEG_BBC_BAS_ADDR_REG);
  show_register(MJPEG_BBC_END_ADDR_REG);
  show_register(MJPEG_BBC_WR_PTR_REG);
  show_register(MJPEG_BBC_RD_PTR_REG);
  show_register(MJPEG_BBC_CUR_POS_REG);
  show_register(MJPEG_BBC_DATA_CNT_REG);
  show_register(MJPEG_BBC_EXT_ADDR_REG);
  show_register(MJPEG_BBC_INT_ADDR_REG);
  show_register(MJPEG_BBC_BAS_ADDR_REG);
  show_register(MJPEG_GBU_BPTR_REG);
  show_register(MJPEG_GBU_WPTR_REG);
  show_register(MJPEG_GBU_BBSR_REG);
  show_register(MJPEG_GBU_CTRL_REG);
  show_register(MJPEG_GBU_BBER_REG);
  show_register(MJPEG_GBU_BBIR_REG);
  show_register(MJPEG_GBU_BBHR_REG);
  show_register(MJPEG_PIC_CTRL_REG);
  show_register(MJPEG_SCL_INFO_REG);
  show_register(MJPEG_DPB_CONFIG_REG);
  show_register(MJPEG_RST_INTVAL_REG);
  show_register(MJPEG_RST_INDEX_REG);
  show_register(MJPEG_BBC_STRM_CTRL_REG);
  show_register(MJPEG_BBC_CTRL_REG);
  show_register(MJPEG_OP_INFO_REG);
  show_register(MJPEG_PIC_SIZE_REG);
  show_register(MJPEG_ROT_INFO_REG);
  show_register(MJPEG_MCU_INFO_REG);
  show_register(MJPEG_GBU_CTRL_REG);
  show_register(MJPEG_DPB_BASE00_REG);
  show_register(MJPEG_DPB_BASE01_REG);
  show_register(MJPEG_DPB_BASE02_REG);
  show_register(MJPEG_DPB_YSTRIDE_REG);
  show_register(MJPEG_DPB_CSTRIDE_REG);
  show_register(MJPEG_PIC_START_REG);
  show_register(MJPEG_CYCLE_INFO_REG);
  show_register(MJPEG_PIC_STATUS_REG);

  show_register(JPU_MMU_TRI);
  return 0;
}

Int32 JPU_WaitInterrupt(JpgHandle handle, int timeout) {
  Uint32 val;
  Uint32 instPicStatusRegAddr;
  Int32 instRegIndex;

  Int32 reason = 0;

  JpgInst *pJpgInst = (JpgInst *)handle;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  instPicStatusRegAddr = ((instRegIndex * NPT_REG_SIZE) + MJPEG_PIC_STATUS_REG);

  reason = jdi_wait_interrupt(pJpgInst->devctx, timeout, instPicStatusRegAddr,
                              instRegIndex);
  if (reason == -1) {
    JLOG(ERR, "JPU time out !!!\n");
    JPU_ShowRegisters(handle);
    if (pJpgInst->sliceInstMode == FALSE) {
      SetJpgPendingInstEx(0, pJpgInst->devctx, pJpgInst->instIndex);
      JpgLeaveLock(pJpgInst->devctx);
    }

    return -1;
  }

  if (reason & (1 << INT_JPU_ERROR)) {
    reason = -2;
  }

  return reason;
}

JpgRet JPU_Init(int dev_id, JdiDeviceCtx *ctx) {
  jpu_instance_pool_t *pjip;
  Uint32 val;
  JdiDeviceCtx devctx;

  devctx = jdi_init(dev_id);
  if (!devctx) {
    return JPG_RET_FAILURE;
  }

  if (ctx != NULL) {
    *ctx = devctx;
  }

  if (jdi_get_task_num(devctx) > 1) {
    return JPG_RET_CALLED_BEFORE;
  }

  JpgEnterLock(devctx);
  pjip = (jpu_instance_pool_t *)jdi_get_instance_pool(devctx);
  if (!pjip) {
    JpgLeaveLock(devctx);
    return JPG_RET_FAILURE;
  }

  // jdi_log(JDI_LOG_CMD_INIT, 1, 0);
  JPU_SWReset(NULL, devctx);
  // JpuWriteReg(devctx, MJPEG_INST_CTRL_START_REG, (1<<0));
  val = JpuReadInstReg(devctx, 0, MJPEG_VERSION_INFO_REG);
  // JPU Capabilities
  g_JpuAttributes.productId = (val >> 24) & 0xf;
  g_JpuAttributes.revisoin = (val & 0xffffff);
  g_JpuAttributes.support12bit = (val >> 28) & 0x01;

  // jdi_log(JDI_LOG_CMD_INIT, 0, 0);
  JpgLeaveLock(devctx);
  return JPG_RET_SUCCESS;
}

void JPU_DeInit(JdiDeviceCtx devctx) {
  JpgEnterLock(devctx);
  if (jdi_get_task_num(devctx) == 1) {
    JpuWriteReg(devctx, MJPEG_INST_CTRL_START_REG, 0);
  }
  JpgLeaveLock(devctx);
  jdi_release(devctx);
}

JpgRet JPU_GetVersionInfo(JdiDeviceCtx devctx, Uint32 *apiVersion,
                          Uint32 *hwRevision, Uint32 *hwProductId) {
  if (JPU_IsInit(devctx) == 0) {
    return JPG_RET_NOT_INITIALIZED;
  }

  JpgEnterLock(devctx);
  if (apiVersion) {
    *apiVersion = API_VERSION;
  }
  if (hwRevision) {
    *hwRevision = g_JpuAttributes.revisoin;
  }
  if (hwProductId) {
    *hwProductId = g_JpuAttributes.productId;
  }
  JpgLeaveLock(devctx);
  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecOpen(JdiDeviceCtx devctx, JpgDecHandle *pHandle,
                   JpgDecOpenParam *pop) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgDecOpenParam(pop);
  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }

  JpgEnterLock(devctx);
  ret = GetJpgInstance(devctx, &pJpgInst);
  if (ret == JPG_RET_FAILURE) {
    *pHandle = 0;
    JpgLeaveLock(devctx);
    return JPG_RET_FAILURE;
  }

  *pHandle = pJpgInst;

  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  memset(pDecInfo, 0x00, sizeof(JpgDecInfo));

  pDecInfo->streamBufSize = pop->bitstreamBufferSize;
  pDecInfo->streamEndian = pop->streamEndian;
  pDecInfo->frameEndian = pop->frameEndian;
  pDecInfo->chromaInterleave = pop->chromaInterleave;
  pDecInfo->packedFormat = pop->packedFormat;
  pDecInfo->roiEnable = pop->roiEnable;
  pDecInfo->roiWidth = pop->roiWidth;
  pDecInfo->roiHeight = pop->roiHeight;
  pDecInfo->roiOffsetX = pop->roiOffsetX;
  pDecInfo->roiOffsetY = pop->roiOffsetY;
  pDecInfo->sliceHeight = pop->sliceHeight;
  pJpgInst->sliceInstMode = pop->sliceInstMode;
  pDecInfo->intrEnableBit = pop->intrEnableBit;
  pDecInfo->decSlicePosY = 0;
  pDecInfo->rotationIndex = pop->rotation / 90;
  pDecInfo->mirrorIndex = pop->mirror;
  /* convert output format */
  switch (pop->outputFormat) {
    case FORMAT_400:
      ret = JPG_RET_INVALID_PARAM;
      break;
    case FORMAT_420:
      pDecInfo->ofmt = O_FMT_420;
      break;
    case FORMAT_422:
      pDecInfo->ofmt = O_FMT_422;
      break;
    case FORMAT_440:
      ret = JPG_RET_INVALID_PARAM;
      break;
    case FORMAT_444:
      pDecInfo->ofmt = O_FMT_444;
      break;
    case FORMAT_MAX:
      pDecInfo->ofmt = O_FMT_NONE;
      break;
    default:
      ret = JPG_RET_INVALID_PARAM;
      break;
  }

  pDecInfo->userqMatTab = 0;
  pDecInfo->decIdx = 0;

  JpgLeaveLock(devctx);

  return ret;
}

JpgRet JPU_DecClose(JpgDecHandle handle) {
  JpgInst *pJpgInst;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;

  JpgEnterLock(pJpgInst->devctx);

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }

  FreeJpgInstance(pJpgInst);
  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecGetInitialInfo(JpgDecHandle handle, JpgDecInitialInfo *info) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  if (info == 0) {
    return JPG_RET_INVALID_PARAM;
  }
  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  if (JpegDecodeHeader(pDecInfo, pJpgInst->devctx) <= 0) return JPG_RET_FAILURE;
  if (pDecInfo->jpg12bit == TRUE && g_JpuAttributes.support12bit == FALSE) {
    return JPG_RET_NOT_SUPPORT;
  }

  info->picWidth = pDecInfo->picWidth;
  info->picHeight = pDecInfo->picHeight;
  info->minFrameBufferCount = 1;
  info->sourceFormat = (FrameFormat)pDecInfo->format;
  info->ecsPtr = pDecInfo->ecsPtr;

  pDecInfo->initialInfoObtained = 1;
  pDecInfo->minFrameBufferNum = 1;

  if ((pDecInfo->packedFormat == PACKED_FORMAT_444) &&
      (pDecInfo->format != FORMAT_444)) {
    return JPG_RET_INVALID_PARAM;
  }

  if (pDecInfo->roiEnable) {
    if (pDecInfo->format == FORMAT_400) {
      pDecInfo->roiMcuWidth = pDecInfo->roiWidth / 8;
    } else {
      pDecInfo->roiMcuWidth = pDecInfo->roiWidth / pDecInfo->mcuWidth;
    }
    pDecInfo->roiMcuHeight = pDecInfo->roiHeight / pDecInfo->mcuHeight;
    pDecInfo->roiMcuOffsetX = pDecInfo->roiOffsetX / pDecInfo->mcuWidth;
    pDecInfo->roiMcuOffsetY = pDecInfo->roiOffsetY / pDecInfo->mcuHeight;

    if ((pDecInfo->roiOffsetX > pDecInfo->alignedWidth) ||
        (pDecInfo->roiOffsetY > pDecInfo->alignedHeight) ||
        (pDecInfo->roiOffsetX + pDecInfo->roiWidth > pDecInfo->alignedWidth) ||
        (pDecInfo->roiOffsetY + pDecInfo->roiHeight > pDecInfo->alignedHeight))
      return JPG_RET_INVALID_PARAM;

    if (pDecInfo->format == FORMAT_400) {
      if (((pDecInfo->roiOffsetX + pDecInfo->roiWidth) < 8) ||
          ((pDecInfo->roiOffsetY + pDecInfo->roiHeight) < pDecInfo->mcuHeight))
        return JPG_RET_INVALID_PARAM;
    } else {
      if (((pDecInfo->roiOffsetX + pDecInfo->roiWidth) < pDecInfo->mcuWidth) ||
          ((pDecInfo->roiOffsetY + pDecInfo->roiHeight) < pDecInfo->mcuHeight))
        return JPG_RET_INVALID_PARAM;
    }

    if (pDecInfo->format == FORMAT_400)
      pDecInfo->roiFrameWidth = pDecInfo->roiMcuWidth * 8;
    else
      pDecInfo->roiFrameWidth = pDecInfo->roiMcuWidth * pDecInfo->mcuWidth;
    pDecInfo->roiFrameHeight = pDecInfo->roiMcuHeight * pDecInfo->mcuHeight;
    info->roiFrameWidth = pDecInfo->roiFrameWidth;
    info->roiFrameHeight = pDecInfo->roiFrameHeight;
    info->roiFrameOffsetX = pDecInfo->roiMcuOffsetX * pDecInfo->mcuWidth;
    info->roiFrameOffsetY = pDecInfo->roiMcuOffsetY * pDecInfo->mcuHeight;
    info->roiMCUSize = pDecInfo->mcuWidth;
  }
  info->colorComponents = pDecInfo->compNum;
  info->bitDepth = pDecInfo->bitDepth;

  /* Decide output format */

  if (pDecInfo->sliceHeight == 0) {
    pDecInfo->sliceHeight = pDecInfo->alignedHeight;
  }

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecRegisterFrameBuffer(JpgDecHandle handle,
                                  FrameBufferInfo *bufArray, int num,
                                  int stride) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;

  if (!pDecInfo->initialInfoObtained) {
    return JPG_RET_WRONG_CALL_SEQUENCE;
  }

  if (bufArray == 0) {
    return JPG_RET_INVALID_FRAME_BUFFER;
  }

  if (num < pDecInfo->minFrameBufferNum) {
    return JPG_RET_INSUFFICIENT_FRAME_BUFFERS;
  }

  if ((stride % 8) != 0) {
    return JPG_RET_INVALID_STRIDE;
  }

  pDecInfo->frameBufPool = bufArray;
  pDecInfo->numFrameBuffers = num;
  pDecInfo->stride = stride;
  pDecInfo->stride_c = bufArray[0].strideC;

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecGetBitstreamBuffer(JpgDecHandle handle, PhysicalAddress *prdPtr,
                                 PhysicalAddress *pwrPtr, int *size) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;
  PhysicalAddress rdPtr;
  PhysicalAddress wrPtr;
  int room;
  Int32 instRegIndex;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex) == pJpgInst) {
    rdPtr =
        JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_RD_PTR_REG);
  } else {
    rdPtr = pDecInfo->streamRdPtr;
  }

  wrPtr = pDecInfo->streamWrPtr;

  if (wrPtr == pDecInfo->streamBufStartAddr) {
    if (pDecInfo->frameOffset == 0) {
      room = (pDecInfo->streamBufEndAddr - pDecInfo->streamBufStartAddr);
    } else {
      room = (pDecInfo->frameOffset);
    }
  } else {
    room = (pDecInfo->streamBufEndAddr - wrPtr);
  }

  room = ((room >> 9) << 9);  // multiple of 512

  if (prdPtr) *prdPtr = rdPtr;
  if (pwrPtr) *pwrPtr = wrPtr;
  if (size) *size = room;

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecUpdateBitstreamBuffer(JpgDecHandle handle, int size) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  PhysicalAddress wrPtr;
  PhysicalAddress rdPtr;
  JpgRet ret;
  int val = 0;
  Int32 instRegIndex;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  wrPtr = pDecInfo->streamWrPtr;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  if (size == 0) {
    val = (wrPtr - pDecInfo->streamBufStartAddr) / 256;
    if ((wrPtr - pDecInfo->streamBufStartAddr) % 256) val = val + 1;
    // if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex) ==
    // pJpgInst) {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_STRM_CTRL_REG,
                    (1UL << 31 | val));
    //}
    pDecInfo->streamEndflag = 1;
    return JPG_RET_SUCCESS;
  }

  wrPtr = pDecInfo->streamWrPtr;
  wrPtr += size;

  if (wrPtr == pDecInfo->streamBufEndAddr) {
    wrPtr = pDecInfo->streamBufStartAddr;
  }

  pDecInfo->streamWrPtr = wrPtr;

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex) == pJpgInst) {
    rdPtr =
        JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_RD_PTR_REG);

    if (rdPtr == pDecInfo->streamBufEndAddr) {
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_CUR_POS_REG, 0);
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_TCNT_REG, 0);
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, (MJPEG_GBU_TCNT_REG + 4),
                      0);
    }

    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_WR_PTR_REG,
                    wrPtr);
    if (wrPtr == pDecInfo->streamBufStartAddr) {
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_END_ADDR_REG,
                      pDecInfo->streamBufEndAddr);
    } else {
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_END_ADDR_REG,
                      wrPtr);
    }
  } else {
    rdPtr = pDecInfo->streamRdPtr;
  }

  pDecInfo->streamRdPtr = rdPtr;

  return JPG_RET_SUCCESS;
}

JpgRet JPU_SWReset(JpgHandle handle, JdiDeviceCtx devctx) {
  Uint32 val;

  int clock_state;
  JpgInst *pJpgInst;
  JdiDeviceCtx instCtx;

  if ((!handle || handle && !handle->devctx) && !devctx) {
    return JPG_RET_FAILURE;
  }

  if (handle && handle->devctx) {
    instCtx = handle->devctx;
  } else {
    instCtx = devctx;
  }

  pJpgInst = ((JpgInst *)handle);
  clock_state = jdi_get_clock_gate(instCtx);
  if (clock_state == 0) {
    jdi_set_clock_gate(instCtx, 1);
  }

  /* 2.2.2.1 - MJPEG_PIC_START_REG
   * [1] - initialize encoder/decoder stateus
   */
  val = 0x1 << JPG_START_INIT;
  JpuWriteReg(instCtx, MJPEG_PIC_START_REG, val);

  do {
    val = JpuReadReg(instCtx, MJPEG_PIC_START_REG);
    if (!(val & (0x1 << 1))) {
      break;
    }
  } while (1);
  if (handle) jdi_log(JDI_LOG_CMD_RESET, 0, pJpgInst->instIndex);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_HWReset(JdiDeviceCtx devctx) {
  if (jdi_hw_reset(devctx) < 0) return JPG_RET_FAILURE;

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecSetRdPtr(JpgDecHandle handle, PhysicalAddress addr,
                       BOOL updateWrPtr) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = (JpgInst *)handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;

  JpgEnterLock(pJpgInst->devctx);

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }
  pDecInfo->streamRdPtr = addr;
  if (updateWrPtr) pDecInfo->streamWrPtr = addr;

  pDecInfo->frameOffset = addr - pDecInfo->streamBufStartAddr;
  pDecInfo->consumeByte = 0;

  JpuWriteReg(pJpgInst->devctx, MJPEG_BBC_RD_PTR_REG, pDecInfo->streamRdPtr);

  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecSetRdPtrEx(JpgDecHandle handle, PhysicalAddress addr,
                         BOOL updateWrPtr) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = (JpgInst *)handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;

  JpgEnterLock(pJpgInst->devctx);

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }
  pDecInfo->streamRdPtr = addr;
  pDecInfo->streamBufStartAddr = addr;
  if (updateWrPtr) pDecInfo->streamWrPtr = addr;

  pDecInfo->frameOffset = 0;
  pDecInfo->consumeByte = 0;

  JpuWriteReg(pJpgInst->devctx, MJPEG_BBC_RD_PTR_REG, pDecInfo->streamRdPtr);

  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecStartOneFrame(JpgDecHandle handle, JpgDecParam *param) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;
  Uint32 val;
  PhysicalAddress rdPtr, wrPtr;
  BOOL is12Bit = FALSE;
  BOOL ppuEnable = FALSE;
  Int32 instRegIndex;
  BOOL bTableInfoUpdate;
  Uint32 dataSize = 0;
  Uint32 appendingSize = 0;
  Uint32 frame_virt_addr = 0;
  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  is12Bit = (pDecInfo->bitDepth == 8) ? FALSE : TRUE;

  if (pDecInfo->frameBufPool ==
      0) {  // This means frame buffers have not been registered.
    return JPG_RET_WRONG_CALL_SEQUENCE;
  }

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  JpgEnterLock(pJpgInst->devctx);
  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex) == pJpgInst) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }
  val = (pDecInfo->frameIdx % pDecInfo->numFrameBuffers);
  frame_virt_addr = pDecInfo->frameBufPool[val].dmaBuffer.viraddr;
  if (pDecInfo->frameOffset < 0) {
    SetJpgPendingInstEx(0, pJpgInst->devctx, pJpgInst->instIndex);
    return JPG_RET_EOS;
  }

  pDecInfo->q_prec0 = 0;
  pDecInfo->q_prec1 = 0;
  pDecInfo->q_prec2 = 0;
  pDecInfo->q_prec3 = 0;

  // check for stream empty case
  if (pDecInfo->streamEndflag == 0) {
    rdPtr = pDecInfo->streamRdPtr;
    wrPtr = pDecInfo->streamWrPtr;
    if (wrPtr == pDecInfo->streamBufStartAddr)
      wrPtr = pDecInfo->streamBufEndAddr;
    if (rdPtr > wrPtr) {  // wrap-around case
      if (((pDecInfo->streamBufEndAddr - rdPtr) +
           (wrPtr - pDecInfo->streamBufStartAddr)) < 1024) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_BIT_EMPTY;
      }
    } else {
      if (wrPtr - rdPtr < 1024) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_BIT_EMPTY;
      }
    }
  }
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_INTR_MASK_REG,
                  ((~pDecInfo->intrEnableBit) & 0x3ff));

  if (pDecInfo->streamRdPtr == pDecInfo->streamBufEndAddr) {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_CUR_POS_REG, 0);
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_TCNT_REG, 0);
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, (MJPEG_GBU_TCNT_REG + 4),
                    0);
  }

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_WR_PTR_REG,
                  pDecInfo->streamWrPtr);
  if (pDecInfo->streamWrPtr == pDecInfo->streamBufStartAddr) {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_END_ADDR_REG,
                    pDecInfo->streamBufEndAddr);
  } else {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_END_ADDR_REG,
                    JPU_CEIL(256, pDecInfo->streamWrPtr));
  }

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_BAS_ADDR_REG,
                  pDecInfo->streamBufStartAddr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_TCNT_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, (MJPEG_GBU_TCNT_REG + 4), 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_ERRMB_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_CTRL_REG,
                  pDecInfo->huffAcIdx << 10 | pDecInfo->huffDcIdx << 7 |
                      pDecInfo->userHuffTab << 6 |
                      (JPU_CHECK_WRITE_RESPONSE_BVALID_SIGNAL << 2) | 0);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_SIZE_REG,
                  (pDecInfo->alignedWidth << 16) | pDecInfo->alignedHeight);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_OP_INFO_REG,
                  pDecInfo->busReqNum);
  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_MCU_INFO_REG,
      (pDecInfo->mcuBlockNum & 0x0f) << 16 | (pDecInfo->compNum & 0x07) << 12 |
          (pDecInfo->compInfo[0] & 0x3f) << 8 |
          (pDecInfo->compInfo[1] & 0x0f) << 4 | (pDecInfo->compInfo[2] & 0x0f));
  // enable intlv NV12: 10, NV21: 11
  // packedFormat:0 => 4'd0
  // packedFormat:1,2,3,4 => 4, 5, 6, 7,
  // packedFormat:5 => 8
  // packedFormat:6 => 9
  val =
      (pDecInfo->frameEndian << 6) | ((pDecInfo->chromaInterleave == 0)   ? 0
                                      : (pDecInfo->chromaInterleave == 1) ? 2
                                                                          : 3);
  if (pDecInfo->packedFormat == PACKED_FORMAT_NONE) {
    val |= (0 << 5) | (0 << 4);
  } else if (pDecInfo->packedFormat == PACKED_FORMAT_444) {
    val |= (1 << 5) | (0 << 4) | (0 << 2);
  } else {
    val |= (0 << 5) | (1 << 4) | ((pDecInfo->packedFormat - 1) << 2);
  }
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_CONFIG_REG, val);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_RST_INTVAL_REG,
                  pDecInfo->rstIntval);

  if (param) {
    if (param->scaleDownRatioWidth > 0)
      pDecInfo->iHorScaleMode = param->scaleDownRatioWidth;
    if (param->scaleDownRatioHeight > 0)
      pDecInfo->iVerScaleMode = param->scaleDownRatioHeight;
  }
  if (pDecInfo->iHorScaleMode | pDecInfo->iVerScaleMode)
    val = ((pDecInfo->iHorScaleMode & 0x3) << 2) |
          ((pDecInfo->iVerScaleMode & 0x3)) | 0x10;
  else {
    val = 0;
  }
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_SCL_INFO_REG, val);

  bTableInfoUpdate = FALSE;
  if (pDecInfo->userHuffTab) {
    bTableInfoUpdate = TRUE;
  }

  if (bTableInfoUpdate == TRUE) {
    if (is12Bit == TRUE) {
      if (!JpgDecHuffTabSetUp_12b(pDecInfo, pJpgInst->devctx, instRegIndex)) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_INVALID_PARAM;
      }
    } else {
      if (!JpgDecHuffTabSetUp(pDecInfo, pJpgInst->devctx, instRegIndex)) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_INVALID_PARAM;
      }
    }
  }

  bTableInfoUpdate = TRUE;  // it always should be TRUE for multi-instance
  if (bTableInfoUpdate == TRUE) {
    if (!JpgDecQMatTabSetUp(pDecInfo, pJpgInst->devctx, instRegIndex)) {
      JpgLeaveLock(pJpgInst->devctx);
      return JPG_RET_INVALID_PARAM;
    }
  }

  JpgDecGramSetup(pDecInfo, pJpgInst->devctx, instRegIndex);

  if (pDecInfo->streamEndflag == 1) {
    val =
        JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_STRM_CTRL_REG);
    if ((val & (1UL << 31)) == 0) {
      val = (pDecInfo->streamWrPtr - pDecInfo->streamBufStartAddr) / 256;
      if ((pDecInfo->streamWrPtr - pDecInfo->streamBufStartAddr) % 256)
        val = val + 1;
      JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_STRM_CTRL_REG,
                      (1UL << 31 | val));
    }
  } else {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_STRM_CTRL_REG, 0);
  }

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_RST_INDEX_REG,
                  0);  // RST index at the beginning.
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_RST_COUNT_REG, 0);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPCM_DIFF_Y_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPCM_DIFF_CB_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPCM_DIFF_CR_REG, 0);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_FF_RPTR_REG,
                  pDecInfo->bitPtr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_CTRL_REG, 3);

  ppuEnable = (pDecInfo->rotationIndex > 0) || (pDecInfo->mirrorIndex > 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_ROT_INFO_REG,
                  (ppuEnable << 4) | (pDecInfo->mirrorIndex << 2) |
                      pDecInfo->rotationIndex);

  val = (pDecInfo->frameIdx % pDecInfo->numFrameBuffers);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE00_REG,
                  frame_virt_addr + pDecInfo->frameBufPool[val].yOffset);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE01_REG,
                  frame_virt_addr + pDecInfo->frameBufPool[val].uOffset);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE02_REG,
                  frame_virt_addr + pDecInfo->frameBufPool[val].vOffset);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_YSTRIDE_REG,
                  pDecInfo->stride);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_CSTRIDE_REG,
                  pDecInfo->stride_c);

  if (pDecInfo->roiEnable) {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_CLP_INFO_REG, 1);
    JpuWriteInstReg(
        pJpgInst->devctx, instRegIndex, MJPEG_CLP_BASE_REG,
        pDecInfo->roiOffsetX << 16 | pDecInfo->roiOffsetY);  // pixel unit
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_CLP_SIZE_REG,
                    (pDecInfo->roiFrameWidth << 16) |
                        pDecInfo->roiFrameHeight);  // pixel Unit
  } else {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_CLP_INFO_REG, 0);
  }
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_START_REG,
                  (1 << JPG_START_PIC));
  pDecInfo->decIdx++;

  SetJpgPendingInstEx(pJpgInst, pJpgInst->devctx, pJpgInst->instIndex);
  if (pJpgInst->sliceInstMode == TRUE) {
    JpgLeaveLock(pJpgInst->devctx);
  }
  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecGetOutputInfo(JpgDecHandle handle, JpgDecOutputInfo *info) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;
  Uint32 val = 0;
  Int32 instRegIndex;
  Uint32 intStatus;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }

  if (info == NULL) {
    return JPG_RET_INVALID_PARAM;
  }

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  if (pJpgInst->sliceInstMode == TRUE) {
    JpgEnterLock(pJpgInst->devctx);
  }

  if (pJpgInst != GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_WRONG_CALL_SEQUENCE;
  }

  if (pDecInfo->frameOffset < 0) {
    info->numOfErrMBs = 0;
    info->decodingSuccess = 1;
    info->indexFrameDisplay = -1;
    SetJpgPendingInstEx(0, pJpgInst->devctx, pJpgInst->instIndex);

    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_SUCCESS;
  }

  if (pDecInfo->roiEnable) {
    info->decPicWidth = pDecInfo->roiMcuWidth * pDecInfo->mcuWidth;
    info->decPicHeight = pDecInfo->roiMcuHeight * pDecInfo->mcuHeight;
  } else {
    info->decPicWidth = pDecInfo->alignedWidth;
    info->decPicHeight = pDecInfo->alignedHeight;
  }

  info->decPicWidth >>= pDecInfo->iHorScaleMode;
  info->decPicHeight >>= pDecInfo->iVerScaleMode;

  info->indexFrameDisplay = (pDecInfo->frameIdx % pDecInfo->numFrameBuffers);
  info->consumedByte =
      (JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_TCNT_REG)) / 8;
  pDecInfo->streamRdPtr =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_RD_PTR_REG);
  pDecInfo->consumeByte = info->consumedByte - 16 - pDecInfo->ecsPtr;
  info->bytePosFrameStart = pDecInfo->frameOffset;
  info->ecsPtr = pDecInfo->ecsPtr;
  info->rdPtr = pDecInfo->streamRdPtr;
  info->wrPtr =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_WR_PTR_REG);

  pDecInfo->ecsPtr = 0;
  pDecInfo->frameIdx++;

  intStatus = info->intStatus;
  //   intStatus = JpuReadInstReg(instRegIndex, MJPEG_PIC_STATUS_REG);
  if (intStatus & (1 << INT_JPU_DONE)) {
    info->decodingSuccess = 1;
    info->numOfErrMBs = 0;
    info->decodeState = DECODE_STATE_FRAME_DONE;
    pDecInfo->decSlicePosY = 0;
  } else if (intStatus & (1 << INT_JPU_ERROR)) {
    info->numOfErrMBs =
        JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_ERRMB_REG);
    info->decodingSuccess = 0;

  } else if (intStatus & (1 << INT_JPU_OVERFLOW)) {
    info->decodeState = DECODE_STATE_SLICE_DONE;
    info->decodedSliceYPos =
        JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_SLICE_POS_REG);
    pDecInfo->decSlicePosY = info->decodedSliceYPos;
  }
  info->frameCycle =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_CYCLE_INFO_REG);

  // if (val != 0)
  //    JpuWriteInstReg(instRegIndex, MJPEG_PIC_STATUS_REG, val);

  if (pJpgInst->loggingEnable) jdi_log(JDI_LOG_CMD_PICRUN, 0, instRegIndex);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_START_REG, 0);

  val = JpuReadReg(pJpgInst->devctx, MJPEG_INST_CTRL_STATUS_REG);
  val &= ~(1UL << instRegIndex);
  JpuWriteReg(pJpgInst->devctx, MJPEG_INST_CTRL_STATUS_REG, val);

  SetJpgPendingInstEx(0, pJpgInst->devctx, pJpgInst->instIndex);
  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_DecGiveCommand(JpgDecHandle handle, JpgCommand cmd, void *param) {
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  switch (cmd) {
    case SET_JPG_SCALE_HOR: {
      int scale;
      scale = *(int *)param;
      if (pDecInfo->alignedWidth < 128 || pDecInfo->alignedHeight < 128) {
        if (scale) {
          return JPG_RET_INVALID_PARAM;
        }
      }

      pDecInfo->iHorScaleMode = scale;
      break;
    }
    case SET_JPG_SCALE_VER: {
      int scale;
      scale = *(int *)param;
      if (pDecInfo->alignedWidth < 128 || pDecInfo->alignedHeight < 128) {
        if (scale) {
          return JPG_RET_INVALID_PARAM;
        }
      }
      pDecInfo->iVerScaleMode = scale;
      break;
    }
    case ENABLE_LOGGING: {
      pJpgInst->loggingEnable = 1;
    } break;
    case DISABLE_LOGGING: {
      pJpgInst->loggingEnable = 0;
    } break;
    default:
      return JPG_RET_INVALID_COMMAND;
  }
  return JPG_RET_SUCCESS;
}

/******************************************************************************
EncOpenParam Initialization
******************************************************************************/

JpgRet JPU_EncOpenParamDefault(JpgEncOpenParam *pEncOP) {
  int ret;
  EncMjpgParam mjpgParam;

  memset(&mjpgParam, 0x00, sizeof(EncMjpgParam));

  pEncOP->jpg12bit = FALSE;
  pEncOP->q_prec0 = FALSE;
  pEncOP->q_prec1 = FALSE;
  pEncOP->tiledModeEnable = 0;
  pEncOP->restartInterval = 0;
  pEncOP->frameEndian = JDI_LITTLE_ENDIAN;
  pEncOP->streamEndian = JDI_LITTLE_ENDIAN;
  pEncOP->intrEnableBit =
      (1 << INT_JPU_DONE) | (1 << INT_JPU_ERROR) | (1 << INT_JPU_BIT_BUF_FULL);
  pEncOP->mirror = 0;
  pEncOP->rotation = 0;
  pEncOP->sliceHeight = 0;
  pEncOP->sliceInstMode = 0;
  ret = JPUEncGetHuffTable(NULL, &mjpgParam, pEncOP->jpg12bit);
  if (ret == 0) return JPG_RET_FAILURE;
  ret = JPUEncGetQMatrix(NULL, &mjpgParam);
  if (ret == 0) return JPG_RET_FAILURE;

  memcpy(pEncOP->huffVal, mjpgParam.huffVal, 4 * 256);
  memcpy(pEncOP->huffBits, mjpgParam.huffBits, 4 * 256);
  memcpy(pEncOP->qMatTab, mjpgParam.qMatTab, 4 * 64 * sizeof(short));

  // Currently only 2DC,2AC huffman table for 12-bit case
  // So, copy them to EX1 to EX2 to modeling rest 2 tables
  if (pEncOP->jpg12bit == TRUE) {
    memcpy(&pEncOP->huffVal[4][0], &pEncOP->huffVal[0][0], 4 * 256);
    memcpy(&pEncOP->huffBits[4][0], &pEncOP->huffBits[0][0], 4 * 256);
  }

  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncHandleRotaion(JpgEncInfo *pEncInfo, Uint32 rotationIndex) {
  BOOL rotation_90_270 = FALSE;
  FrameFormat format;
  Uint32 mcuWidth, mcuHeight;
  Uint32 comp0McuWidth, comp0McuHeight;

  if (rotationIndex == 1 || rotationIndex == 3) {
    if (pEncInfo->format == FORMAT_422)
      format = FORMAT_440;
    else if (pEncInfo->format == FORMAT_440)
      format = FORMAT_422;
    else
      format = pEncInfo->format;
    rotation_90_270 = TRUE;
  } else {
    format = pEncInfo->format;
  }

  // Picture size alignment
  if (format == FORMAT_420 || format == FORMAT_422) {
    pEncInfo->alignedWidth = JPU_CEIL(16, pEncInfo->picWidth);
    mcuWidth = 16;
  } else {
    pEncInfo->alignedWidth = JPU_CEIL(8, pEncInfo->picWidth);
    mcuWidth = (format == FORMAT_400) ? 32 : 8;
  }

  if (format == FORMAT_420 || format == FORMAT_440) {
    pEncInfo->alignedHeight = JPU_CEIL(16, pEncInfo->picHeight);
    mcuHeight = 16;
  } else {
    pEncInfo->alignedHeight = JPU_CEIL(8, pEncInfo->picHeight);
    mcuHeight = 8;
  }

  pEncInfo->mcuWidth = mcuWidth;
  pEncInfo->mcuHeight = mcuHeight;
  if (format == FORMAT_400) {
    if (rotationIndex == 1 || rotationIndex == 3) {
      pEncInfo->mcuWidth = mcuHeight;
      pEncInfo->mcuHeight = mcuWidth;
    }
  }

  comp0McuWidth = pEncInfo->mcuWidth;
  comp0McuHeight = pEncInfo->mcuHeight;
  if (rotation_90_270 == TRUE) {
    if (pEncInfo->format == FORMAT_420 || pEncInfo->format == FORMAT_422) {
      comp0McuWidth = 16;
    } else {
      comp0McuWidth = 8;
    }

    if (pEncInfo->format == FORMAT_420 || pEncInfo->format == FORMAT_440) {
      comp0McuHeight = 16;
    } else if (pEncInfo->format == FORMAT_400) {
      comp0McuHeight = 32;
    } else {
      comp0McuHeight = 8;
    }
  }
  if (pEncInfo->format == FORMAT_400) {
    pEncInfo->compInfo[1] = 0;
    pEncInfo->compInfo[2] = 0;
  } else {
    pEncInfo->compInfo[1] = 5;
    pEncInfo->compInfo[2] = 5;
  }

  if (pEncInfo->format == FORMAT_400) {
    pEncInfo->compNum = 1;
  } else
    pEncInfo->compNum = 3;

  if (pEncInfo->format == FORMAT_420) {
    pEncInfo->mcuBlockNum = 6;
  } else if (pEncInfo->format == FORMAT_422) {
    pEncInfo->mcuBlockNum = 4;
  } else if (pEncInfo->format == FORMAT_440) { /* aka YUV440 */
    pEncInfo->mcuBlockNum = 4;
  } else if (pEncInfo->format == FORMAT_444) {
    pEncInfo->mcuBlockNum = 3;
  } else if (pEncInfo->format == FORMAT_400) {
    Uint32 picHeight = (rotationIndex == 1 || rotationIndex == 3)
                           ? pEncInfo->picWidth
                           : pEncInfo->picHeight;
    if (0 < pEncInfo->rstIntval && picHeight == pEncInfo->sliceHeight) {
      pEncInfo->mcuBlockNum = 1;
      comp0McuWidth = 8;
      comp0McuHeight = 8;
    } else {
      pEncInfo->mcuBlockNum = 4;
    }
  }
  pEncInfo->compInfo[0] = 0xa;
  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncOpen(JdiDeviceCtx devctx, JpgEncHandle *pHandle,
                   JpgEncOpenParam *pop) {
  JpgInst *pJpgInst;
  JpgEncInfo *pEncInfo;
  JpgRet ret;
  Uint32 i, j;
  BOOL rotation_90_270 = FALSE;

  ret = CheckJpgEncOpenParam(pop, &g_JpuAttributes);
  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }

  JpgEnterLock(devctx);
  ret = GetJpgInstance(devctx, &pJpgInst);
  if (ret == JPG_RET_FAILURE) {
    JpgLeaveLock(devctx);
    return JPG_RET_FAILURE;
  }

  *pHandle = pJpgInst;
  pEncInfo = &pJpgInst->JpgInfo->encInfo;
  memset(pEncInfo, 0x00, sizeof(JpgEncInfo));

  // pEncInfo->streamRdPtr = pop->bitstreamBuffer;
  // pEncInfo->streamWrPtr = pop->bitstreamBuffer;
  pEncInfo->streamFd = pop->bitstreamBufferFd;
  pEncInfo->streamBodyOffset = pop->bitstreamBodyOffset;
  pEncInfo->sliceHeight = pop->sliceHeight;
  pEncInfo->intrEnableBit = pop->intrEnableBit;

  // pEncInfo->streamBufStartAddr = pop->bitstreamBuffer;
  // pEncInfo->streamBufEndAddr   = pop->bitstreamBuffer +
  // pop->bitstreamBufferSize - (4096);
  pEncInfo->streamSize = pop->bitstreamBufferSize;
  pEncInfo->streamEndian = pop->streamEndian;
  pEncInfo->frameEndian = pop->frameEndian;
  pEncInfo->chromaInterleave = pop->chromaInterleave;
  pEncInfo->format = pop->sourceFormat;
  pEncInfo->srcWidth = pop->srcWidth;
  pEncInfo->srcHeight = pop->srcHeight;
  pEncInfo->picWidth = pop->picWidth;
  pEncInfo->picHeight = pop->picHeight;
  pEncInfo->disableAPPMarker = 0;
  pEncInfo->disableSOIMarker = 0;
  pEncInfo->stuffByteEnable = 1;

  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }
  pEncInfo->rotationIndex = pop->rotation / 90;
  JPU_EncHandleRotaion(pEncInfo, pEncInfo->rotationIndex);
  if (pop->rotation == 90 || pop->rotation == 270) {
    rotation_90_270 = TRUE;
  }
#if 0
    if (pop->rotation == 90 || pop->rotation == 270) {
        if (pEncInfo->format == FORMAT_422)      format = FORMAT_440;
        else if (pEncInfo->format == FORMAT_440) format = FORMAT_422;
        else                                     format = pEncInfo->format;
        rotation_90_270 = TRUE;
    }

    else {
        format = pEncInfo->format;
    }
    // Picture size alignment
    if (format == FORMAT_420 || format == FORMAT_422) {
        pEncInfo->alignedWidth = JPU_CEIL(16, pEncInfo->picWidth);
        mcuWidth = 16;
    }
    else {
        pEncInfo->alignedWidth = JPU_CEIL(8, pEncInfo->picWidth);
        mcuWidth = (format == FORMAT_400) ? 32 : 8;
    }

    if (format == FORMAT_420 || format == FORMAT_440) {
        pEncInfo->alignedHeight = JPU_CEIL(16, pEncInfo->picHeight);
        mcuHeight = 16;
    }
    else {
        pEncInfo->alignedHeight = JPU_CEIL(8, pEncInfo->picHeight);
        mcuHeight = 8;
    }

    pEncInfo->mcuWidth  = mcuWidth;
    pEncInfo->mcuHeight = mcuHeight;
    if (format == FORMAT_400) {
        if (pop->rotation == 90 || pop->rotation == 270) {
            pEncInfo->mcuWidth  = mcuHeight;
            pEncInfo->mcuHeight = mcuWidth;
        }
    }

    comp0McuWidth  = pEncInfo->mcuWidth;
    comp0McuHeight = pEncInfo->mcuHeight;
    if (rotation_90_270 == TRUE) {
        if (pEncInfo->format == FORMAT_420 || pEncInfo->format == FORMAT_422) {
            comp0McuWidth = 16;
        }
        else  {
            comp0McuWidth = 8;
        }

        if (pEncInfo->format == FORMAT_420 || pEncInfo->format == FORMAT_440) {
            comp0McuHeight = 16;
        }
        else if (pEncInfo->format == FORMAT_400) {
            comp0McuHeight = 32;
        }
        else {
            comp0McuHeight = 8;
        }
    }
#endif
  if (pop->sliceInstMode == TRUE) {
    Uint32 ppuHeight = (rotation_90_270 == TRUE) ? pEncInfo->alignedWidth
                                                 : pEncInfo->alignedHeight;
    if (pop->sliceHeight % pEncInfo->mcuHeight) {
      JpgLeaveLock(pJpgInst->devctx);
      return JPG_RET_INVALID_PARAM;
    }

    if (pop->sliceHeight > ppuHeight) {
      JpgLeaveLock(pJpgInst->devctx);
      return JPG_RET_INVALID_PARAM;
    }

    if (pop->sliceHeight < pEncInfo->mcuHeight) {
      JpgLeaveLock(pJpgInst->devctx);
      return JPG_RET_INVALID_PARAM;
    }
  }

  pJpgInst->sliceInstMode = pop->sliceInstMode;
  pEncInfo->rstIntval = pop->restartInterval;
  pEncInfo->jpg12bit = pop->jpg12bit;
  pEncInfo->q_prec0 = pop->q_prec0;
  pEncInfo->q_prec1 = pop->q_prec1;
  if (pop->jpg12bit) {
    for (i = 0; i < 8; i++) {
      memcpy(pEncInfo->pHuffVal[i], pop->huffVal[i], 256);
      memcpy(pEncInfo->pHuffBits[i], pop->huffBits[i], 256);
#if 0
            for (j=0; j<256; j++) {
                pEncInfo->pHuffVal[i][j] = pop->huffVal[i][j];
                pEncInfo->pHuffBits[i][j] = pop->huffBits[i][j];
            }
#endif
    }
  } else {
    for (i = 0; i < 4; i++) {
      memcpy(pEncInfo->pHuffVal[i], pop->huffVal[i], 256);
      memcpy(pEncInfo->pHuffBits[i], pop->huffBits[i], 256);
#if 0
            for(j=0; j<256; j++) {
                pEncInfo->pHuffVal[i][j] = pop->huffVal[i][j];
                pEncInfo->pHuffBits[i][j] = pop->huffBits[i][j];
            }
#endif
    }
  }

  for (i = 0; i < 4; i++) {
    for (int j = 0; j < 64; j++) {
      pEncInfo->pQMatTab[i][InvScanTable[j]] = pop->qMatTab[i][j];
    }
  }

  pEncInfo->pCInfoTab[0] = sJpuCompInfoTable[2];
  pEncInfo->pCInfoTab[1] = pEncInfo->pCInfoTab[0] + 6;
  pEncInfo->pCInfoTab[2] = pEncInfo->pCInfoTab[1] + 6;
  pEncInfo->pCInfoTab[3] = pEncInfo->pCInfoTab[2] + 6;

  if (pop->packedFormat == PACKED_FORMAT_444 &&
      pEncInfo->format != FORMAT_444) {
    return JPG_RET_INVALID_PARAM;
  }

  pEncInfo->packedFormat = pop->packedFormat;
#if 0
    if (pEncInfo->format == FORMAT_400) {
        pEncInfo->compInfo[1] = 0;
        pEncInfo->compInfo[2] = 0;
    }
    else {
        pEncInfo->compInfo[1] = 5;
        pEncInfo->compInfo[2] = 5;
    }

    if (pEncInfo->format == FORMAT_400) {
        pEncInfo->compNum = 1;
    }
    else
        pEncInfo->compNum = 3;

    if (pEncInfo->format == FORMAT_420) {
        pEncInfo->mcuBlockNum = 6;
    }
    else if (pEncInfo->format == FORMAT_422) {
        pEncInfo->mcuBlockNum = 4;
    } else if (pEncInfo->format == FORMAT_440) { /* aka YUV440 */
        pEncInfo->mcuBlockNum = 4;
    } else if (pEncInfo->format == FORMAT_444) {
        pEncInfo->mcuBlockNum = 3;
    } else if (pEncInfo->format == FORMAT_400) {
        Uint32 picHeight = (90 == pop->rotation || 270 == pop->rotation) ? pEncInfo->picWidth : pEncInfo->picHeight;
        if (0 < pEncInfo->rstIntval && picHeight == pEncInfo->sliceHeight) {
            pEncInfo->mcuBlockNum = 1;
            comp0McuWidth         = 8;
            comp0McuHeight        = 8;
        }
        else {
            pEncInfo->mcuBlockNum = 4;
        }
    }
    pEncInfo->compInfo[0] = (comp0McuWidth >> 3) << 3 | (comp0McuHeight >> 3);
    //pEncInfo->compInfo[0] = 0xa;
#endif
  pEncInfo->busReqNum =
      (pop->jpg12bit == FALSE)
          ? GetEnc8bitBusReqNum(pEncInfo->packedFormat, pEncInfo->format)
          : GetEnc12bitBusReqNum(pEncInfo->packedFormat, pEncInfo->format);

  pEncInfo->tiledModeEnable = pop->tiledModeEnable;

  pEncInfo->encIdx = 0;
  pEncInfo->encSlicePosY = 0;
  pEncInfo->mirrorIndex = pop->mirror;
  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncClose(void *pHandle) {
  JpgInst *pJpgInst = (JpgInst *)pHandle;
  JpgRet ret;

  ret = CheckJpgInstValidity(pHandle);
  if (ret != JPG_RET_SUCCESS) return ret;
  JpgEnterLock(pJpgInst->devctx);

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }

  FreeJpgInstance(pJpgInst);

  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncStartOneFrame(JpgEncHandle handle, JpgEncParam *param) {
  JpgInst *pJpgInst;
  JpgEncInfo *pEncInfo;
  FrameBufferInfo *pBasFrame;
  JpgRet ret;
  Uint32 val;
  Int32 instRegIndex;
  BOOL bTableInfoUpdate;
  Uint32 rotMirEnable = 0;
  Uint32 rotMirMode = 0;
  Uint32 dataSize = 0;
  Uint32 appendingSize = 0;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pEncInfo = &pJpgInst->JpgInfo->encInfo;

  ret = CheckJpgEncParam(handle, param);
  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }

  pBasFrame = param->sourceFrame;

  JpgEnterLock(pJpgInst->devctx);

  if (GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex) == pJpgInst) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_FRAME_NOT_COMPLETE;
  }

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }
  /***************************************************************************************
   * check whether it needs to append buffer when mirror or rotation happened *
   *     1.normal       2. padding on left   3. padding on top       4. both *
   *    ##########@        @##########         @@@@@@@@@@@          @@@@@@@@@@@
   * *
   *    ##########@        @##########         ##########@          @##########
   * *
   *    ##########@        @##########         ##########@          @##########
   * *
   *    @@@@@@@@@@@        @@@@@@@@@@@         ##########@          @##########
   * *
   *                                                                                     *
   *  after mirror or rotation  2/3/4 should append dma pages to mmu for the bug
   * 128642  *
   * ************************************************************************************/
  dataSize = pEncInfo->alignedWidth * pEncInfo->alignedHeight * 3 / 2;
  if ((pEncInfo->mirrorIndex == 1 && pEncInfo->rotationIndex == 1) ||
      (pEncInfo->mirrorIndex == 3 && pEncInfo->rotationIndex == 1) ||
      (pEncInfo->mirrorIndex == 0 && pEncInfo->rotationIndex == 3) ||
      (pEncInfo->mirrorIndex == 1 && pEncInfo->rotationIndex == 0) ||
      (pEncInfo->mirrorIndex == 2 && pEncInfo->rotationIndex == 2)) {
    appendingSize = (pEncInfo->alignedHeight - pEncInfo->srcHeight) *
                    pEncInfo->alignedWidth;
  } else if ((pEncInfo->mirrorIndex == 1 && pEncInfo->rotationIndex == 2) ||
             (pEncInfo->mirrorIndex == 2 && pEncInfo->rotationIndex == 0) ||
             (pEncInfo->mirrorIndex == 0 && pEncInfo->rotationIndex == 1) ||
             (pEncInfo->mirrorIndex == 3 && pEncInfo->rotationIndex == 3)) {
    appendingSize = (pEncInfo->alignedWidth - pEncInfo->srcWidth);
  } else if ((pEncInfo->mirrorIndex == 0 && pEncInfo->rotationIndex == 2) ||
             (pEncInfo->mirrorIndex == 1 && pEncInfo->rotationIndex == 1) ||
             (pEncInfo->mirrorIndex == 2 && pEncInfo->rotationIndex == 3) ||
             (pEncInfo->mirrorIndex == 3 && pEncInfo->rotationIndex == 0)) {
    appendingSize = (pEncInfo->alignedHeight - pEncInfo->srcHeight) *
                    pEncInfo->alignedWidth;
    appendingSize += (pEncInfo->alignedWidth - pEncInfo->srcWidth);
  }

  // JLOG(INFO, "######## input fd:%d output fd:%d slice mode:%d slice y:%d
  // algin height:%d #########
  // \n",param->sourceFrame->dmaBuffer.fd,pEncInfo->streamFd,pJpgInst->sliceInstMode,pEncInfo->encSlicePosY,pEncInfo->alignedHeight);
  JPU_DMA_CFG cfg =
      jdi_config_mmu(pJpgInst->devctx, param->sourceFrame->dmaBuffer.fd,
                     pEncInfo->streamFd, dataSize, appendingSize);
  // JLOG(INFO, "######## input va:%x output va:%x #########
  // \n",cfg.intput_virt_addr,cfg.output_virt_addr);
  if (cfg.intput_virt_addr == 0 || cfg.output_virt_addr == 0) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_INVALID_PARAM;
  }
  pEncInfo->streamRdPtr = cfg.output_virt_addr + pEncInfo->streamBodyOffset;
  pEncInfo->streamWrPtr = cfg.output_virt_addr + pEncInfo->streamBodyOffset;
  pEncInfo->streamBufStartAddr =
      cfg.output_virt_addr + pEncInfo->streamBodyOffset;
  pEncInfo->streamBufEndAddr =
      cfg.output_virt_addr + pEncInfo->streamSize + pEncInfo->streamBodyOffset;
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_INTR_MASK_REG,
                  ((~pEncInfo->intrEnableBit) & 0x7ff));
  // JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_SLICE_INFO_REG,
  // pEncInfo->sliceHeight); JpuWriteInstReg(pJpgInst->devctx, instRegIndex,
  // MJPEG_SLICE_DPB_POS_REG, pEncInfo->picHeight); // assume that the all of
  // source buffer is available JpuWriteInstReg(pJpgInst->devctx, instRegIndex,
  // MJPEG_SLICE_POS_REG, pEncInfo->encSlicePosY);
  val = (0 << 16) | (pEncInfo->encSlicePosY / pEncInfo->mcuHeight);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_SETMB_REG, 0);

  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_CLP_INFO_REG,
      0);  // off ROI enable due to not supported feature for encoder.

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_BAS_ADDR_REG,
                  pEncInfo->streamBufStartAddr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_END_ADDR_REG,
                  pEncInfo->streamBufEndAddr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_WR_PTR_REG,
                  pEncInfo->streamWrPtr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_RD_PTR_REG,
                  pEncInfo->streamRdPtr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_CUR_POS_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_DATA_CNT_REG,
                  JPU_GBU_SIZE / 4);  // 64 * 4 byte == 32 * 8 byte
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_EXT_ADDR_REG,
                  pEncInfo->streamBufStartAddr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_INT_ADDR_REG, 0);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_BAS_ADDR_REG,
                  pEncInfo->streamWrPtr);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_EXT_ADDR_REG,
                  pEncInfo->streamRdPtr);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_BPTR_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_WPTR_REG, 0);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_BBSR_REG, 0);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_CTRL_REG, 8);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_BBER_REG,
                  ((JPU_GBU_SIZE / 4) * 2) - 1);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_BBIR_REG,
                  JPU_GBU_SIZE / 4);  // 64 * 4 byte == 32 * 8 byte
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_GBU_BBHR_REG,
                  JPU_GBU_SIZE / 4);  // 64 * 4 byte == 32 * 8 byte

  //#define DEFAULT_TDI_TAI_DATA 0x055
  //    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_CTRL_REG,
  //    (pEncInfo->jpg12bit<<31) | (pEncInfo->q_prec0<<30) |
  //    (pEncInfo->q_prec1<<29) | (pEncInfo->tiledModeEnable<<19) |
  //                                                     (DEFAULT_TDI_TAI_DATA<<7)
  //                                                     | 0x18 | (1<<6) |
  //                                                     (JPU_CHECK_WRITE_RESPONSE_BVALID_SIGNAL<<2));
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_CTRL_REG,
                  1 << 6 | 1 << 4 | 1 << 3);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_SCL_INFO_REG, 0);

  val = 0;
  // PackMode[3:0]: 0(NONE), 8(PACK444), 4,5,6,7(YUYV => VYUY)
  val =
      (pEncInfo->frameEndian << 6) | ((pEncInfo->chromaInterleave == 0)   ? 0
                                      : (pEncInfo->chromaInterleave == 1) ? 2
                                                                          : 3);
  if (pEncInfo->packedFormat == PACKED_FORMAT_NONE) {
    val |= (0 << 5) | (0 << 4) | (0 << 2);
  } else if (pEncInfo->packedFormat == PACKED_FORMAT_444) {
    val |= (1 << 5) | (0 << 4) | (0 << 2);
  } else {
    val |= (0 << 5) | (1 << 4) | ((pEncInfo->packedFormat - 1) << 2);
  }

  // JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_CONFIG_REG, val);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_CONFIG_REG,
                  2);  // yuv NV12

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_RST_INTVAL_REG,
                  pEncInfo->rstIntval);
  if (pEncInfo->encSlicePosY == 0) {
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_RST_INDEX_REG,
                    0);  // RST index from 0.
    JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_STRM_CTRL_REG,
                    0);  // clear BBC ctrl status.
  }

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_CTRL_REG,
                  (pEncInfo->streamEndian << 1) | 1);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_OP_INFO_REG,
                  pEncInfo->busReqNum);

  bTableInfoUpdate = FALSE;
  if (pEncInfo->encSlicePosY == 0) {
    bTableInfoUpdate =
        TRUE;  // if sliceMode is disabled. HW process frame by frame between
               // instances. so Huff table can be updated between instances.
               // instances can have the different HuffTable.
  }

  if (bTableInfoUpdate == TRUE) {
    // Load HUFFTab
    if (pEncInfo->jpg12bit) {
      if (!JpgEncLoadHuffTab_12b(pJpgInst, instRegIndex)) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_INVALID_PARAM;
      }
    } else {
      if (!JpgEncLoadHuffTab(pJpgInst, instRegIndex)) {
        JpgLeaveLock(pJpgInst->devctx);
        return JPG_RET_INVALID_PARAM;
      }
    }
  }

  bTableInfoUpdate = FALSE;
  if (pEncInfo->encSlicePosY == 0) {
    bTableInfoUpdate = TRUE;
  }

  if (bTableInfoUpdate == TRUE) {
    // Load QMATTab
    if (!JpgEncLoadQMatTab(pJpgInst, instRegIndex)) {
      JpgLeaveLock(pJpgInst->devctx);
      return JPG_RET_INVALID_PARAM;
    }
  }

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_SIZE_REG,
                  pEncInfo->alignedWidth << 16 | pEncInfo->alignedHeight);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_ROT_INFO_REG, 0);

  if (pEncInfo->rotationIndex || pEncInfo->mirrorIndex) {
    rotMirEnable = 0x10;
    rotMirMode = (pEncInfo->mirrorIndex << 2) | pEncInfo->rotationIndex;
  }
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_ROT_INFO_REG,
                  rotMirEnable | rotMirMode);
  JLOG(DBG,
       "##mcuBlockNum %x compNum :%x  compInfo0:%x compInfo1:%x  compInfo2:%x "
       "rotMirMode:%d  ### \n",
       pEncInfo->mcuBlockNum, pEncInfo->compNum, pEncInfo->compInfo[0],
       pEncInfo->compInfo[1], pEncInfo->compInfo[2], rotMirMode);

  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_MCU_INFO_REG,
      (pEncInfo->mcuBlockNum & 0x0f) << 16 | (pEncInfo->compNum & 0x07) << 12 |
          (pEncInfo->compInfo[0] & 0x3f) << 8 |
          (pEncInfo->compInfo[1] & 0x0f) << 4 | (pEncInfo->compInfo[2] & 0x0f));

  // JpgEncGbuResetReg
  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_GBU_CTRL_REG,
      pEncInfo->stuffByteEnable << 3);  // stuffing "FF" data where frame end

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE00_REG,
                  cfg.intput_virt_addr);
  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE01_REG,
      cfg.intput_virt_addr + pBasFrame->uOffset +
          appendingSize /
              2);  // appendingSize will not zero only when case 2/3/4 above
  JpuWriteInstReg(
      pJpgInst->devctx, instRegIndex, MJPEG_DPB_BASE02_REG,
      cfg.intput_virt_addr + pBasFrame->vOffset +
          appendingSize /
              2);  // appendingSize will not zero only when case 2/3/4 above
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_YSTRIDE_REG,
                  pBasFrame->stride);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_DPB_CSTRIDE_REG,
                  pBasFrame->stride);
  JLOG(DBG, "### y:%x uv:%x  ystride:%d cstride:%d ### \n",
       cfg.intput_virt_addr,
       cfg.intput_virt_addr +
           pBasFrame->stride * JPU_CEIL(8, pEncInfo->picHeight),
       pBasFrame->stride, pBasFrame->strideC);

  if (pJpgInst->loggingEnable) jdi_log(JDI_LOG_CMD_PICRUN, 1, instRegIndex);

  // JPU_ShowRegisters(handle);
  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_START_REG,
                  (1 << JPG_START_PIC));

  pEncInfo->encIdx++;

  SetJpgPendingInstEx(pJpgInst, pJpgInst->devctx, pJpgInst->instIndex);

  JpgLeaveLock(pJpgInst->devctx);

  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncGetOutputInfo(void *handle, JpgEncOutputInfo *info) {
  JpgInst *pJpgInst;
  JpgEncInfo *pEncInfo;
  Uint32 val;
  Uint32 intReason;
  JpgRet ret;
  Int32 instRegIndex;

  pJpgInst = (JpgInst *)handle;
  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) {
    return ret;
  }

  if (info == 0) {
    return JPG_RET_INVALID_PARAM;
  }

  pEncInfo = &pJpgInst->JpgInfo->encInfo;

  if (pJpgInst->sliceInstMode == TRUE) {
    JpgEnterLock(pJpgInst->devctx);
  }
  if (pJpgInst != GetJpgPendingInstEx(pJpgInst->devctx, pJpgInst->instIndex)) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_WRONG_CALL_SEQUENCE;
  }

  if (pJpgInst->sliceInstMode == TRUE) {
    instRegIndex = pJpgInst->instIndex;
  } else {
    instRegIndex = 0;
  }

  info->frameCycle =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_CYCLE_INFO_REG);
  //    intReason = JpuReadInstReg(instRegIndex, MJPEG_PIC_STATUS_REG);
  intReason = info->intStatus;

  if ((intReason & 0x4) >> 2) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_WRONG_CALL_SEQUENCE;
  }

  info->encodedSliceYPos =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_SLICE_POS_REG);
  pEncInfo->encSlicePosY = info->encodedSliceYPos;
  if (intReason & (1 << INT_JPU_DONE)) pEncInfo->encSlicePosY = 0;

  PhysicalAddress streamWrPtr =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_WR_PTR_REG);
  PhysicalAddress streamRdPtr =
      JpuReadInstReg(pJpgInst->devctx, instRegIndex, MJPEG_BBC_RD_PTR_REG);
  info->bitstreamBufferFd = pEncInfo->streamFd;
  info->bitstreamSize = streamWrPtr - streamRdPtr;
  info->streamWrPtr = streamWrPtr;
  info->streamRdPtr = streamRdPtr;

  if (intReason != 0) {
    //   JpuWriteInstReg(instRegIndex, MJPEG_PIC_STATUS_REG, intReason);

    if (intReason & (1 << INT_JPU_DONE))
      info->encodeState = ENCODE_STATE_FRAME_DONE;
  }

  if (pJpgInst->loggingEnable) jdi_log(JDI_LOG_CMD_PICRUN, 0, instRegIndex);

  JpuWriteInstReg(pJpgInst->devctx, instRegIndex, MJPEG_PIC_START_REG, 0);

  SetJpgPendingInstEx(0, pJpgInst->devctx, pJpgInst->instIndex);

  JpgLeaveLock(pJpgInst->devctx);
  return JPG_RET_SUCCESS;
}

JpgRet JPU_EncGiveCommand(JpgEncHandle handle, JpgCommand cmd, void *param) {
  JpgInst *pJpgInst;
  JpgEncInfo *pEncInfo;
  JpgRet ret;

  ret = CheckJpgInstValidity(handle);
  if (ret != JPG_RET_SUCCESS) return ret;

  pJpgInst = handle;
  pEncInfo = &pJpgInst->JpgInfo->encInfo;
  switch (cmd) {
    case ENC_JPG_GET_HEADER: {
      if (param == 0) {
        return JPG_RET_INVALID_PARAM;
      }

      if (!JpgEncEncodeHeader(handle, param)) {
        return JPG_RET_INVALID_PARAM;
      }
      break;
    }

    case SET_JPG_USE_STUFFING_BYTE_FF: {
      int enable;
      enable = *(int *)param;
      pEncInfo->stuffByteEnable = enable;
      break;
    }

    case ENABLE_LOGGING: {
      pJpgInst->loggingEnable = 1;
    } break;
    case DISABLE_LOGGING: {
      pJpgInst->loggingEnable = 0;
    } break;

    default:
      return JPG_RET_INVALID_COMMAND;
  }
  return JPG_RET_SUCCESS;
}
