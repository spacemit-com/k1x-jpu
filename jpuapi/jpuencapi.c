/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */
#include "jpuencapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "BufferAllocatorWrapper.h"
#include "jpuapi.h"
#include "jpuapifunc.h"
#include "jpulog.h"
#include "jputypes.h"

/* CODAJ12 Constraints
 * The minimum value of Qk is 8 for 16bit quantization element, 2 for 8bit
 * quantization element
 */
#define MIN_Q16_ELEMENT 8
#define MIN_Q8_ELEMENT 2
JpgEncOpenParam encOpenParam = {0};

JpgRet AsrJpuEncOpen(void **handle, EncOpenParam *param) {
  JdiDeviceCtx devctx = NULL;
  JpgRet ret;
  Uint32 apiVersion;
  Uint32 hwRevision;
  Uint32 hwProductId;

  JpgEncHandle *pEncHandler;
  JpgEncHandle encHandler;

  pEncHandler = (JpgEncHandle *)handle;
  JPU_EncOpenParamDefault(&encOpenParam);
  encOpenParam.chromaInterleave = param->chromaInterleave;
  encOpenParam.packedFormat = param->packedFormat;
  encOpenParam.picHeight = param->picHeight;
  encOpenParam.picWidth = param->picWidth;
  encOpenParam.srcHeight = param->picHeight;
  encOpenParam.srcWidth = param->picWidth;
  encOpenParam.sourceFormat = param->sourceFormat;
  encOpenParam.jpg12bit = param->jpg12bit;

  ret = JPU_Init(0, &devctx);
  if (ret != JPG_RET_SUCCESS && ret != JPG_RET_CALLED_BEFORE) {
    JLOG(ERR, "JPU_Init failed Error code is 0x%x \n", ret);
    goto ERR_ENC_INIT;
  }

  if (!devctx) {
    JLOG(ERR, "Fail to create JPU device !!!\n");
    goto ERR_ENC_INIT;
  }

  JLOG(DBG, "JPU device@%p create\n", devctx);

  JPU_GetVersionInfo(devctx, &apiVersion, &hwRevision, &hwProductId);
  JLOG(DBG,
       "JPU Version API_VERSION=0x%x, HW_REVISION=%d, HW_PRODUCT_ID=0x%x\n",
       apiVersion, hwRevision, hwProductId);
  if (hwProductId != PRODUCT_ID_CODAJ10) {
    JLOG(ERR,
         "Error JPU HW_PRODUCT_ID=0x%x is not match with API_VERSION=0x%x\n",
         hwProductId, apiVersion);
    goto ERR_ENC_INIT;
  }
  ret = JPU_EncOpen(devctx, &encHandler, &encOpenParam);
  *pEncHandler = encHandler;
  return ret;
ERR_ENC_INIT:
  JPU_DeInit(devctx);
  return JPG_RET_FAILURE;
}
static int JpuEncQualityFactor(JpgEncInfo *encInfo, Uint32 quality,
                               BOOL useStdTable) {
  Uint32 scaleFactor;
  Uint32 i;
  Uint32 temp;
  /* These are the sample quantization tables given in JPEG spec section K.1.
   * The spec says that the values given produce "good" quality, and
   * when divided by 2, "very good" quality.
   */
  static const Uint32 std_luminance_quant_tbl[64] = {
      16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
      14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
      18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
      49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
  static const Uint32 std_chrominance_quant_tbl[64] = {
      17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
      24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
      99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
  Uint32 pLumaQTable[64];
  Uint32 pChromaQTable[64];
  Uint32 qSize = 1, qTableSize;
  Uint32 minQvalue = MIN_Q8_ELEMENT;
  if (quality <= 0) quality = 1;
  if (quality > 100) quality = 100;

  for (i = 0; i < 64; i++) {
    pLumaQTable[i] = (useStdTable == TRUE)
                         ? std_luminance_quant_tbl[i]
                         : (Int32)encInfo->pQMatTab[DC_TABLE_INDEX0][i];
  }
  for (i = 0; i < 64; i++) {
    pChromaQTable[i] = (useStdTable == TRUE)
                           ? std_chrominance_quant_tbl[i]
                           : (Int32)encInfo->pQMatTab[AC_TABLE_INDEX0][i];
  }

  minQvalue = (encInfo->jpg12bit == TRUE) ? MIN_Q16_ELEMENT : MIN_Q8_ELEMENT;

  /* The basic table is used as-is (scaling 100) for a quality of 50.
   * Qualities 50..100 are converted to scaling percentage 200 - 2*Q;
   * note that at Q=100 the scaling is 0, which will cause jpeg_add_quant_table
   * to make all the table entries 1 (hence, minimum quantization loss).
   * Qualities 1..50 are converted to scaling percentage 5000/Q.
   */
  if (quality < 50)
    scaleFactor = 5000 / quality;
  else
    scaleFactor = 200 - quality * 2;

  for (i = 0; i < 64; i++) {
    temp = (pLumaQTable[i] * scaleFactor + 50) / 100;
    /* limit the values to the valid range */
    temp = (temp < minQvalue) ? minQvalue : temp;

    if (temp > 32767) temp = 32767; /* max quantizer needed for 12 bits */
    if (encInfo->q_prec0 == FALSE && temp > 255)
      temp = 255; /* limit to baseline range if requested */
    encInfo->pQMatTab[DC_TABLE_INDEX0][i] = temp;
  }

  for (i = 0; i < 64; i++) {
    temp = (pChromaQTable[i] * scaleFactor + 50) / 100;
    /* limit the values to the valid range */
    temp = (temp < minQvalue) ? minQvalue : temp;

    if (temp > 32767) temp = 32767; /* max quantizer needed for 12 bits */
    if (encInfo->q_prec1 == FALSE && temp > 255)
      temp = 255; /* limit to baseline range if requested */

    encInfo->pQMatTab[AC_TABLE_INDEX0][i] = temp;
  }

  qSize = encInfo->jpg12bit ? 2 : 1;

  // setting of qmatrix table information
#ifdef USE_CNM_DEFAULT_QMAT_TABLE
  memset((void *)encInfo->pQMatTab[DC_TABLE_INDEX0], 0x00, 64 * qSize);
  memcpy((void *)encInfo->pQMatTab[DC_TABLE_INDEX0], (void *)lumaQ2,
         64 * qSize);

  memset((void *)encInfo->pQMatTab[AC_TABLE_INDEX0], 0x00, 64 * qSize);
  memcpy((void *)encInfo->pQMatTab[AC_TABLE_INDEX0], (void *)chromaBQ2,
         64 * qSize);
#endif

  qTableSize = 64 * qSize;
  memcpy((void *)encInfo->pQMatTab[DC_TABLE_INDEX1],
         (void *)encInfo->pQMatTab[DC_TABLE_INDEX0], qTableSize);
  memcpy((void *)encInfo->pQMatTab[AC_TABLE_INDEX1],
         (void *)encInfo->pQMatTab[AC_TABLE_INDEX0], qTableSize);

  return 1;
}

JpgRet AsrJpuEncSetParam(void *handle, Uint32 parameterIndex, void *value) {
  JpgEncInst *pEncHandler = (JpgEncInst *)handle;
  JpgEncInfo *encInfo = &pEncHandler->JpgInfo->encInfo;
  EncMjpgParam *mjpgParam = NULL;
  int i, j;

  mjpgParam = (EncMjpgParam *)malloc(sizeof(EncMjpgParam));
  if (mjpgParam == NULL) {
    JLOG(ERR, "Fail to malloc  mjpgParam !!!\n");
    return JPG_RET_FAILURE;
  }
  memset(mjpgParam, 0x00, sizeof(EncMjpgParam));
  switch (parameterIndex) {
    case JPU_12BIT:
      pEncHandler->JpgInfo->encInfo.jpg12bit = *(Uint32 *)value;
      break;
    case JPU_ROTATION:
      pEncHandler->JpgInfo->encInfo.rotationIndex = *(Uint32 *)value;
      JPU_EncHandleRotaion(&pEncHandler->JpgInfo->encInfo,
                           pEncHandler->JpgInfo->encInfo.rotationIndex);
      break;
    case JPU_MIRROR:
      pEncHandler->JpgInfo->encInfo.mirrorIndex = *(Uint32 *)value;
      break;
    case JPU_QUALITY:
      JpuEncQualityFactor(&pEncHandler->JpgInfo->encInfo, *(Uint32 *)value,
                          FALSE);
      break;
    case JPU_HUFFMAN_TAB:
      JPUEncGetHuffTable((char *)(value), mjpgParam, encInfo->jpg12bit);
      if (encInfo->jpg12bit) {
        for (i = 0; i < 8; i++) {
          memcpy(pEncHandler->JpgInfo->encInfo.pHuffVal[i],
                 mjpgParam->huffVal[i], 256);
          memcpy(pEncHandler->JpgInfo->encInfo.pHuffBits[i],
                 mjpgParam->huffBits[i], 256);
        }
      } else {
        for (i = 0; i < 4; i++) {
          memcpy(pEncHandler->JpgInfo->encInfo.pHuffVal[i],
                 mjpgParam->huffVal[i], 256);
          memcpy(pEncHandler->JpgInfo->encInfo.pHuffBits[i],
                 mjpgParam->huffBits[i], 256);
        }
      }
      break;
    case JPU_QUANT_TAB:
      JPUEncGetQMatrix((char *)value, mjpgParam);
      for (i = 0; i < 4; i++) {
        memcpy(pEncHandler->JpgInfo->encInfo.pQMatTab[i], mjpgParam->qMatTab[i],
               64 * sizeof(short));
      }
      break;
    case JPU_DISABLE_APP_MARKER:
      pEncHandler->JpgInfo->encInfo.disableAPPMarker = *(Uint32 *)value;
      break;
    case JPU_DISABLE_SOI_MARKER:
      pEncHandler->JpgInfo->encInfo.disableSOIMarker = *(Uint32 *)value;
      break;
    case JPU_ENABLE_SOF_STUFFING:
      pEncHandler->JpgInfo->encInfo.stuffByteEnable = *(Uint32 *)value;
      break;
    case JPU_IMAGE_ENDIAN:
      pEncHandler->JpgInfo->encInfo.streamEndian = *(Uint32 *)value;
      break;
    case JPU_FRAME_BUF_ENDIAN:
      pEncHandler->JpgInfo->encInfo.frameEndian = *(Uint32 *)value;
      break;
    default:
      break;
  }
  if (mjpgParam) free(mjpgParam);
  return JPG_RET_SUCCESS;
}

JpgRet AsrJpuEncStartOneFrame(void *handle, FrameBufferInfo *frameBuffer,
                              ImageBufferInfo *jpegImageBuffer) {
  JpgRet ret;
  if (handle == NULL) {
    JLOG(INFO, "%s handle NULL !!!\n", __func__);
    return JPG_RET_INVALID_PARAM;
  }
  JpgEncInst *JpgEncHandle = (JpgEncInst *)handle;
  JpgEncParamSet headerParamSet;
  int imageHeaderSize = 0;
  JpgEncParam encParam = {0};
  JpgEncOutputInfo outputInfo = {0};
  BYTE *imageDataPtr = NULL;
  BYTE *inputDmaBufVir = NULL;
  int int_reason = 0;
  encParam.sourceFrame = frameBuffer;
  headerParamSet.disableAPPMarker =
      JpgEncHandle->JpgInfo->encInfo.disableAPPMarker;
  headerParamSet.enableSofStuffing =
      JpgEncHandle->JpgInfo->encInfo.stuffByteEnable;
  headerParamSet.disableSOIMarker =
      JpgEncHandle->JpgInfo->encInfo.disableSOIMarker;
  headerParamSet.headerMode =
      ENC_HEADER_MODE_NORMAL;  // Encoder header disable/enable control.
                               // Annex:A 1.2.3 item 13
  headerParamSet.quantMode =
      JPG_TBL_NORMAL;  // JPG_TBL_MERGE    // Merge quantization table.
                       // Annex:A 1.2.3 item 7
  headerParamSet.huffMode = JPG_TBL_NORMAL;  // JPG_TBL_MERGE    //Merge huffman
                                             // table. Annex:A 1.2.3 item
  // headerParamSet.pParaSet = (BYTE
  // *)mmap(NULL,jpegImageBuffer->dmaBuffer.size,PROT_READ|PROT_WRITE,MAP_SHARED,jpegImageBuffer->dmaBuffer.fd,0);
  inputDmaBufVir = (BYTE *)mmap(NULL, jpegImageBuffer->dmaBuffer.size,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                jpegImageBuffer->dmaBuffer.fd, 0);
  headerParamSet.pParaSet = inputDmaBufVir + jpegImageBuffer->dataOffset;
  headerParamSet.size =
      jpegImageBuffer->dmaBuffer.size - jpegImageBuffer->dataOffset;
  if (jpegImageBuffer->dmaBuffer.size < 600) {
    JLOG(INFO, "jpeg image buffer can smaller then header !!!\n");
    return JPG_RET_FAILURE;
  }

  ret = JpgEncEncodeHeader(JpgEncHandle, &headerParamSet);
  imageHeaderSize = headerParamSet.size;

  JpgEncHandle->JpgInfo->encInfo.streamFd = jpegImageBuffer->dmaBuffer.fd;
  JpgEncHandle->JpgInfo->encInfo.streamBodyOffset =
      imageHeaderSize + jpegImageBuffer->dataOffset;
  JpgEncHandle->JpgInfo->encInfo.streamSize =
      jpegImageBuffer->dmaBuffer.size -
      JpgEncHandle->JpgInfo->encInfo.streamBodyOffset;
  ret = JPU_EncStartOneFrame(JpgEncHandle, &encParam);

  while (1) {
    int_reason = JPU_WaitInterrupt(JpgEncHandle, JPU_INTERRUPT_TIMEOUT_MS);
    if (int_reason == -1) {
      JLOG(ERR, "Error : inst %d timeout happened\n", JpgEncHandle->instIndex);
      // JPU_SWReset(handle, devctx);
      break;
    }
    if (int_reason & (1 << INT_JPU_ERROR)) {
      JLOG(ERR, "Error: JPU encode error!!!!");
      break;
    }

    if (int_reason & (1 << INT_JPU_DONE)) {  // Must catch PIC_DONE interrupt
                                             // before catching EMPTY interrupt
      // Do no clear INT_JPU_DONE these will be cleared in JPU_EncGetOutputInfo.
      outputInfo.intStatus = int_reason;
      break;
    }
  }

  if ((ret = JPU_EncGetOutputInfo(JpgEncHandle, &outputInfo)) !=
      JPG_RET_SUCCESS) {
    JLOG(ERR, "JPU_EncGetOutputInfo failed Error code is 0x%x \n", ret);
  }
  imageDataPtr =
      headerParamSet.pParaSet + outputInfo.bitstreamSize + imageHeaderSize;
  while (outputInfo.bitstreamSize) {
    if (*(imageDataPtr) == 0xff) {
      outputInfo.bitstreamSize--;
      JLOG(DBG, "%s:find stuff byte :%p value:%x\n", __func__, imageDataPtr,
           *imageDataPtr);
    } else if (*(imageDataPtr) == 0xd9 && *(imageDataPtr - 1) == 0xff) {
      JLOG(DBG, "%s:find EOI \n", __func__);
      break;
    }
    imageDataPtr--;
  }
  jpegImageBuffer->imageSize = outputInfo.bitstreamSize + imageHeaderSize;

  munmap((void *)inputDmaBufVir, jpegImageBuffer->dmaBuffer.size);
  JLOG(DBG, "jpu enc image size:%d \n",
       outputInfo.bitstreamSize + imageHeaderSize);
  if (int_reason == -1 || int_reason & (1 << INT_JPU_ERROR)) {
    ret = JPG_RET_FAILURE;
  }
  return ret;
}

JpgRet AsrJpuEncClose(void *handle) {
  JpgRet ret;
  JpgEncOutputInfo outputInfo = {0};
  JpgInst *pJpgInst;

  pJpgInst = (JpgInst *)handle;

  if (JPU_EncClose(handle) == JPG_RET_FRAME_NOT_COMPLETE) {
    JPU_EncGetOutputInfo(handle, &outputInfo);
    ret = JPU_EncClose(handle);
  }

  JPU_DeInit(pJpgInst->devctx);
  return JPG_RET_SUCCESS;
}