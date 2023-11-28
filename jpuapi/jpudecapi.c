/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */
#include <linux/dma-buf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "jpuapi.h"
#include "jpuapifunc.h"
#include "jpuencapi.h"
#include "jpulog.h"
#include "jputypes.h"

/* CODAJ10 Constraints
 * The minimum value of Qk is 8 for 16bit quantization element, 2 for 8bit
 * quantization element
 */

#define MIN_Q16_ELEMENT 8
#define MIN_Q8_ELEMENT 2
//JpgEncOpenParam encOpenParam = {0};

JpgRet AsrJpuDecOpen(void **handle, DecOpenParam *param) {
  JdiDeviceCtx devctx = NULL;
  JpgRet ret;
  Uint32 apiVersion;
  Uint32 hwRevision;
  Uint32 hwProductId;
  JpgDecHandle *pDecHandler;
  JpgDecHandle decHandler;
  JpgDecOpenParam decOP = {0};
  pDecHandler = (JpgDecHandle *)handle;

  ret = JPU_Init(0, &devctx);
  if (ret != JPG_RET_SUCCESS && ret != JPG_RET_CALLED_BEFORE) {
    JLOG(ERR, "JPU_Init failed Error code is 0x%x \n", ret);
    goto ERR_DEC_INIT;
  }

  decOP.streamEndian = JDI_LITTLE_ENDIAN;
  decOP.frameEndian = JDI_LITTLE_ENDIAN;
  decOP.chromaInterleave = param->chromaInterleave;
  decOP.packedFormat = param->packedFormat;
  decOP.roiEnable = FALSE;
  decOP.roiOffsetX = 0;
  decOP.roiOffsetY = 0;
  decOP.roiWidth = 0;
  decOP.roiHeight = 0;
  decOP.rotation = 0;
  decOP.sliceHeight = 0;
  decOP.mirror = MIRDIR_NONE;
  decOP.outputFormat = FORMAT_MAX;
  decOP.intrEnableBit = ((1 << INT_JPU_DONE) | (1 << INT_JPU_ERROR) |
                         (1 << INT_JPU_BIT_BUF_EMPTY));

  ret = JPU_DecOpen(devctx, &decHandler, &decOP);
  if (ret != JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_DecOpen failed Error code is 0x%x \n", ret);
    goto ERR_DEC_INIT;
  }

  *pDecHandler = decHandler;
  return ret;
ERR_DEC_INIT:
  JPU_DeInit(devctx);
  return JPG_RET_FAILURE;
}
JpgRet AsrJpuDecSetParam(void *handle, Uint32 parameterIndex, void *value) {
  JpgDecInst *pDecHandler = (JpgDecInst *)handle;
  JpgDecInfo decInfo = pDecHandler->JpgInfo->decInfo;
  // to do
  switch (parameterIndex) {
    default:
      break;
  }
  return JPG_RET_SUCCESS;
}

JpgRet AsrJpuDecGetInitialInfo(void *handle, ImageBufferInfo *jpegImageBuffer,
                               JpgDecInitialInfo *info) {
  JpgRet ret;
  Uint32 instIdx;
  JpgDecInfo *pDecInfo;

  JLOG(ERR, "AsrJpuDecGetInitial dma buffer size:%d fd:%d\n",
       jpegImageBuffer->dmaBuffer.size, jpegImageBuffer->dmaBuffer.fd);
  if (handle == NULL) {
    JLOG(ERR, "%s handle invalid !!!\n", __func__);
    return JPG_RET_INVALID_PARAM;
  }
  JpgDecInst *JpgDecHandle = (JpgDecInst *)handle;
  instIdx = JpgDecHandle->instIndex;
  pDecInfo = &JpgDecHandle->JpgInfo->decInfo;
  pDecInfo->streamFd = jpegImageBuffer->dmaBuffer.fd;
  pDecInfo->streamBufSize = jpegImageBuffer->imageSize;
  pDecInfo->pBitStream = (BYTE *)mmap(NULL, jpegImageBuffer->dmaBuffer.size,
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      jpegImageBuffer->dmaBuffer.fd, 0);
  if ((ret = JPU_DecGetInitialInfo(handle, info)) != JPG_RET_SUCCESS) {
    JLOG(ERR, "AsrJpuDecGetInitialInfo failed Error code is 0x%x, inst=%d \n",
         ret, instIdx);
    return JPG_RET_INVALID_PARAM;
  }
  munmap(pDecInfo->pBitStream, jpegImageBuffer->dmaBuffer.size);
  return JPG_RET_SUCCESS;
}

JpgRet AsrJpuDecStartOneFrame(void *handle, FrameBufferInfo *frameBuffer,
                              ImageBufferInfo *jpegImageBuffer,
                              JpgDecInitialInfo *info) {
  JpgRet ret;
  JpgInst *pJpgInst;
  JpgDecInfo *pDecInfo;
  // JpgDecInitialInfo   initialInfo = {0};
  JpgDecParam decParam = {0};
  // FrameFormat subsample;
  JpgDecOutputInfo outputInfo = {0};
  // FrameFormat initialSourceFormat;
  // Uint32 initialOutFormat;
  int int_reason;
  Uint32 instIdx;

  if (handle == NULL) {
    JLOG(INFO, "%s handle NULL !!!\n", __func__);
    return JPG_RET_INVALID_PARAM;
  }
  JpgDecInst *JpgDecHandle = (JpgDecInst *)handle;
  pJpgInst = JpgDecHandle;
  instIdx = pJpgInst->instIndex;
  pDecInfo = &pJpgInst->JpgInfo->decInfo;
  JPU_DMA_CFG cfg =
      jdi_config_mmu(pJpgInst->devctx, jpegImageBuffer->dmaBuffer.fd,
                     frameBuffer->dmaBuffer.fd, frameBuffer->dmaBuffer.size, 0);
  if (cfg.intput_virt_addr == 0 || cfg.output_virt_addr == 0) {
    JpgLeaveLock(pJpgInst->devctx);
    return JPG_RET_INVALID_PARAM;
  }
  pDecInfo->streamRdPtr = cfg.intput_virt_addr;
  pDecInfo->streamWrPtr = cfg.intput_virt_addr;
  pDecInfo->streamBufStartAddr = cfg.intput_virt_addr;
  pDecInfo->streamBufEndAddr =
      cfg.intput_virt_addr + jpegImageBuffer->dmaBuffer.size;
  frameBuffer->dmaBuffer.viraddr = cfg.output_virt_addr;
  if (ret = JPU_DecSetRdPtrEx(handle, pDecInfo->streamWrPtr, TRUE) !=
            JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_DecSetRdPtrEx failed Error code is 0x%x \n", ret);
  }
  // Register frame buffers requested by the decoder.
  if ((ret = JPU_DecRegisterFrameBuffer(
           handle, frameBuffer, 1, frameBuffer->stride)) != JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_DecRegisterFrameBuffer failed Error code is 0x%x \n", ret);
    return JPG_RET_FAILURE;
  }

  if ((ret = JPU_DecUpdateBitstreamBuffer(
           handle, jpegImageBuffer->imageSize
                       ? jpegImageBuffer->imageSize
                       : jpegImageBuffer->dmaBuffer.size)) != JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_DecUpdateBitstreamBuffer failed Error code is 0x%x \n", ret);
    return JPG_RET_FAILURE;
  }
  // Update bitstream EOS
  if ((ret = JPU_DecUpdateBitstreamBuffer(handle, 0)) != JPG_RET_SUCCESS) {
    JLOG(ERR, "Update EOS failed, Error code is 0x%x\n", ret);
    return JPG_RET_FAILURE;
  }

  JPU_DecGiveCommand(handle, SET_JPG_SCALE_HOR, &pDecInfo->iHorScaleMode);
  JPU_DecGiveCommand(handle, SET_JPG_SCALE_VER, &pDecInfo->iVerScaleMode);
  // Start decoding a frame.
  ret = JPU_DecStartOneFrame(handle, &decParam);
  if (ret != JPG_RET_SUCCESS && ret != JPG_RET_EOS) {
    if (ret == JPG_RET_BIT_EMPTY) {
      JLOG(INFO, "BITSTREAM NOT ENOUGH.............\n");
    }

    JLOG(ERR, "JPU_DecStartOneFrame failed Error code is 0x%x \n", ret);
    return JPG_RET_FAILURE;
  }

  while (1) {
    if ((int_reason = JPU_WaitInterrupt(handle, JPU_INTERRUPT_TIMEOUT_MS)) ==
        -1) {
      JLOG(ERR, "Error : timeout happened\n");
      break;
    }
    if (int_reason & ((1 << INT_JPU_DONE) | (1 << INT_JPU_ERROR))) {
      // Do no clear INT_JPU_DONE and INT_JPU_ERROR interrupt. these will be
      // cleared in JPU_DecGetOutputInfo.
      JLOG(INFO, "INSTANCE #%d int_reason: %08x\n", instIdx, int_reason);
      break;
    }
  }

  if ((ret = JPU_DecGetOutputInfo(handle, &outputInfo)) != JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_DecGetOutputInfo failed Error code is 0x%x \n", ret);
    return JPG_RET_FAILURE;
  }

  JLOG(INFO, "%02d %8d %8x %8x %10d %8x %8x %10d\n", instIdx,
       outputInfo.indexFrameDisplay, outputInfo.bytePosFrameStart,
       outputInfo.ecsPtr, outputInfo.consumedByte, outputInfo.rdPtr,
       outputInfo.wrPtr, outputInfo.frameCycle);

  if (outputInfo.numOfErrMBs) {
    Int32 errRstIdx, errPosX, errPosY;
    errRstIdx = (outputInfo.numOfErrMBs & 0x0F000000) >> 24;
    errPosX = (outputInfo.numOfErrMBs & 0x00FFF000) >> 12;
    errPosY = (outputInfo.numOfErrMBs & 0x00000FFF);
    JLOG(ERR, "Error restart Idx : %d, MCU x:%d, y:%d \n", errRstIdx, errPosX,
         errPosY);
  }

  return JPG_RET_SUCCESS;
}

JpgRet AsrJpuDecClose(void *handle) {
  JpgRet ret;
  JpgDecOutputInfo outputInfo;
  JpgInst *pJpgInst;

  pJpgInst = (JpgInst *)handle;
  JPU_DecClose(handle);
  JPU_DeInit(pJpgInst->devctx);
  return JPG_RET_SUCCESS;
}
