/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include <errno.h>
#include <linux/dma-buf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "BufferAllocatorWrapper.h"
#include "jpuencapi.h"
#include "jpulog.h"
#include "main_helper.h"

static int StoreYuvImageBurstFormat_V20(
    BufferAllocator* bufferAllocator, int chromaStride, Uint8* dst,
    int picWidth, int picHeight, Uint32 bitDepth, void* addrY, void* addrCb,
    void* addrCr, int stride, FrameFormat format, CbCrInterLeave intlv,
    PackedFormat packed);

/******************************************************************************
DPB Image Data Control
******************************************************************************/
#define MAX_ROW_IN_MCU 8
static void TiledToLinear(Uint8* pSrc, Uint8* pDst, Uint32 width, Uint32 height,
                          Uint32 cWidth, Uint32 cHeight, Uint32 lineWidth) {
  Uint32 picX, picY;
  Uint32 yuvAddr = 0;
  Uint32 convAddr = 0;
  Uint32 chromaBase = 0;
  Uint32 row = 0;
  Uint32 col = 0;
  Uint32 ncols = width / lineWidth;

  // Luma data.
  for (picY = 0; picY < height; picY++) {
    for (picX = 0; picX < width; picX += lineWidth) {
      convAddr = ((picY / MAX_ROW_IN_MCU) * MAX_ROW_IN_MCU * width) +
                 (row * width) + col * lineWidth;
      memcpy(pDst + convAddr, pSrc + yuvAddr, lineWidth);
      yuvAddr += lineWidth;
      row++;
      row %= 8;
      if (row == 0) col++;
      col %= ncols;
    }
  }
  chromaBase = convAddr + lineWidth;  // add last line

  // Chroma data.
  // lineWidth*2. only support Cb Cr interleaved format.
  lineWidth *= 2;
  col = row = 0;
  convAddr = 0;
  ncols = cWidth / lineWidth;
  for (picY = 0; picY < cHeight; picY++) {
    for (picX = 0; picX < cWidth; picX += lineWidth) {
      convAddr = ((picY / MAX_ROW_IN_MCU) * MAX_ROW_IN_MCU * cWidth) +
                 (row * cWidth) + col * lineWidth;
      memcpy(pDst + chromaBase + convAddr, pSrc + yuvAddr, lineWidth);
      yuvAddr += lineWidth;
      row++;
      row %= 8;
      if (row == 0) col++;
      col %= ncols;
    }
  }
}

int SaveYuvImageHelperFormat_V20(BufferAllocator* bufferAllocator, FILE* yuvFp,
                                 Uint8* pYuv, FrameBufferInfo* fb,
                                 CbCrInterLeave interLeave, PackedFormat packed,
                                 Uint32 picWidth, Uint32 picHeight,
                                 Uint32 bitDepth) {
  int frameSize;
  void* data;

  if (pYuv == NULL) {
    JLOG(ERR, "%s:%d pYuv is NULL\n", __FUNCTION__, __LINE__);
    return 0;
  }
  data = mmap(NULL, fb->dmaBuffer.size, PROT_READ | PROT_WRITE, MAP_SHARED,
              fb->dmaBuffer.fd, 0);
  frameSize = StoreYuvImageBurstFormat_V20(
      bufferAllocator, fb->strideC, pYuv, picWidth, picHeight, bitDepth,
      data + fb->yOffset, data + fb->uOffset, data + fb->vOffset, fb->stride,
      fb->format, interLeave, packed);
  munmap(data, fb->dmaBuffer.size);
  if (yuvFp) {
    if (!fwrite(pYuv, sizeof(Uint8), frameSize, yuvFp)) {
      JLOG(ERR, "Frame Data fwrite failed file handle is 0x%x \n", yuvFp);
      return 0;
    }
    fflush(yuvFp);
  }

  return 1;
}

/******************************************************************************
EncOpenParam Initialization
******************************************************************************/

BOOL GetJpgEncOpenParam(EncOpenParam* pEncOP, EncConfigParam* pEncConfig) {
  ENC_CFG encCfg;
  Int32 ret;

  char cfgPath[MAX_FILE_PATH];
  char* srcYuvFileName = pEncConfig->yuvFileName;

  memset(&encCfg, 0x00, sizeof(ENC_CFG));

  encCfg.prec = pEncConfig->extendedSequential;
  sprintf(cfgPath, "%s/%s", pEncConfig->strCfgDir, pEncConfig->cfgFileName);

  if (parseJpgCfgFile(&encCfg, cfgPath) == 0) {
    return FALSE;
  }
  if (strlen(srcYuvFileName) == 0) {
    strcpy(srcYuvFileName, encCfg.SrcFileName);
  }

  if (encCfg.FrmFormat == 0) {
    pEncConfig->chromaInterleave = CBCR_SEPARATED;
  } else if (encCfg.FrmFormat == 1) {
    pEncConfig->chromaInterleave = CBCR_INTERLEAVE;
  } else if (encCfg.FrmFormat == 2) {
    pEncConfig->chromaInterleave = CRCB_INTERLEAVE;
  } else {
    pEncConfig->chromaInterleave = CBCR_SEPARATED;
  }
  pEncConfig->mjpgFramerate = encCfg.FrameRate;

  /* Source format */
  pEncConfig->packedFormat = (PackedFormat)(encCfg.FrmFormat - 2);
  if (pEncConfig->packedFormat >= PACKED_FORMAT_MAX)
    pEncConfig->packedFormat = PACKED_FORMAT_NONE;

  pEncConfig->outNum = encCfg.NumFrame;

  if (pEncConfig->picWidth == 0) pEncConfig->picWidth = encCfg.PicX;
  if (pEncConfig->picHeight == 0) pEncConfig->picHeight = encCfg.PicY;
  if (pEncOP) {
    pEncOP->picWidth = encCfg.PicX;
    pEncOP->picHeight = encCfg.PicY;
    pEncOP->chromaInterleave = pEncConfig->chromaInterleave;
    pEncOP->packedFormat = pEncConfig->packedFormat;
    pEncOP->sourceFormat = encCfg.SrcFormat;
  }
  return TRUE;
}

//------------------------------------------------------------------------------
// ENCODE PARAMETER PARSE FUNCSIONS
//------------------------------------------------------------------------------
// Parameter parsing helper
static int GetValue(FILE* fp, char* para, char* value) {
  char lineStr[256];
  char paraStr[256];
  fseek(fp, 0, SEEK_SET);

  while (1) {
    if (fgets(lineStr, 256, fp) == NULL) return 0;
    sscanf(lineStr, "%s %s", paraStr, value);
    if (paraStr[0] != ';') {
      if (strcmp(para, paraStr) == 0) return 1;
    }
  }
}

int parseJpgCfgFile(ENC_CFG* pEncCfg, char* FileName) {
  FILE* Fp;
  char sLine[256] = {
      0,
  };
  int res = 0;

  Fp = fopen(FileName, "rt");
  if (Fp == NULL) {
    fprintf(stderr, "   > ERROR: File not exist <%s>, %d\n", FileName, errno);
    goto end;
  }

  // source file name
  if (GetValue(Fp, "YUV_SRC_IMG", sLine) == 0) goto end;
  sscanf(sLine, "%s", (char*)pEncCfg->SrcFileName);

  // frame format
  // 	; 0-planar, 1-NV12,NV16(CbCr interleave) 2-NV21,NV61(CbCr alternative)
  // 		; 3-YUYV, 4-UYVY, 5-YVYU, 6-VYUY, 7-YUV packed (444 only)
  if (GetValue(Fp, "FRAME_FORMAT", sLine) == 0) goto end;
  sscanf(sLine, "%d", &pEncCfg->FrmFormat);

  // width
  if (GetValue(Fp, "PICTURE_WIDTH", sLine) == 0) goto end;
  sscanf(sLine, "%d", &pEncCfg->PicX);

  // height
  if (GetValue(Fp, "PICTURE_HEIGHT", sLine) == 0) goto end;
  sscanf(sLine, "%d", &pEncCfg->PicY);
#if 0
    // frame_rate
    if (GetValue(Fp, "FRAME_RATE", sLine) == 0)
        goto end;
    {
        double frameRate;
        int  timeRes, timeInc;

        frameRate = (double)(int)atoi(sLine);

        timeInc = 1;
        while ((int)frameRate != frameRate) {
            timeInc *= 10;
            frameRate *= 10;
        }
        timeRes = (int) frameRate;
        // divide 2 or 5
        if (timeInc%2 == 0 && timeRes%2 == 0) {
            timeInc /= 2;
            timeRes /= 2;
        }
        if (timeInc%5 == 0 && timeRes%5 == 0) {
            timeInc /= 5;
            timeRes /= 5;
        }

        if (timeRes == 2997 && timeInc == 100) {
            timeRes = 30000;
            timeInc = 1001;
        }
        pEncCfg->FrameRate = (timeInc - 1) << 16;
        pEncCfg->FrameRate |= timeRes;
    }

    // frame count
    if (GetValue(Fp, "FRAME_NUMBER_ENCODED", sLine) == 0)
        goto end;
    sscanf(sLine, "%d", &pEncCfg->NumFrame);

    if (GetValue(Fp, "VERSION_ID", sLine) == 0)
        goto end;
    sscanf(sLine, "%d", &pEncCfg->VersionID);

    if (GetValue(Fp, "RESTART_INTERVAL", sLine) == 0)
        goto end;
    sscanf(sLine, "%d", &pEncCfg->RstIntval);
    if (GetValue(Fp, "HUFFMAN_TABLE", sLine) == 0)
        goto end;
    sscanf(sLine, "%s", (char *)pEncCfg->HuffTabName);
    if (GetValue(Fp, "QMATRIX_TABLE", sLine) == 0)
        goto end;
    sscanf(sLine, "%s", (char *)pEncCfg->QMatTabName);
#endif
  if (GetValue(Fp, "IMG_FORMAT", sLine) == 0) goto end;
  sscanf(sLine, "%d", (Int32*)&pEncCfg->SrcFormat);

  if (GetValue(Fp, "QMATRIX_PREC0", sLine) == 0)
    pEncCfg->QMatPrec0 = 0;
  else
    sscanf(sLine, "%d", &pEncCfg->QMatPrec0);

  if (GetValue(Fp, "QMATRIX_PREC1", sLine) == 0)
    pEncCfg->QMatPrec1 = 0;
  else
    sscanf(sLine, "%d", &pEncCfg->QMatPrec1);
  res = 1;
end:
  if (Fp) fclose(Fp);
  return res;
}

int StoreYuvImageBurstFormat_V20(BufferAllocator* bufferAllocator,
                                 int chromaStride, Uint8* dst, int picWidth,
                                 int picHeight, Uint32 bitDepth, void* addrY,
                                 void* addrCb, void* addrCr, int stride,
                                 FrameFormat format, CbCrInterLeave interLeave,
                                 PackedFormat packed) {
  int size;
  int y, nY = 0, nCb, nCr;
  void* addr;
  int lumaSize, chromaSize = 0, chromaWidth = 0, chromaHeight = 0;
  Uint8* puc;
  int chromaStride_i = 0;
  Uint32 bytesPerPixel = (bitDepth + 7) / 8;

  chromaStride_i = chromaStride;

  switch (format) {
    case FORMAT_420:
      nY = picHeight;
      nCb = nCr = picHeight / 2;
      chromaSize = (picWidth / 2) * (picHeight / 2);
      chromaWidth = picWidth / 2;
      chromaHeight = nY;
      break;
    case FORMAT_440:
      nY = picHeight;
      nCb = nCr = picHeight / 2;
      chromaSize = (picWidth) * (picHeight / 2);
      chromaWidth = picWidth;
      chromaHeight = nY;
      break;
    case FORMAT_422:
      nY = picHeight;
      nCb = nCr = picHeight;
      chromaSize = (picWidth / 2) * picHeight;
      chromaWidth = (picWidth / 2);
      chromaHeight = nY * 2;
      break;
    case FORMAT_444:
      nY = picHeight;
      nCb = nCr = picHeight;
      chromaSize = picWidth * picHeight;
      chromaWidth = picWidth;
      chromaHeight = nY * 2;
      break;
    case FORMAT_400:
      nY = picHeight;
      nCb = nCr = 0;
      chromaSize = 0;
      chromaWidth = 0;
      chromaHeight = 0;
      break;
    default:
      return 0;
  }

  puc = dst;
  addr = addrY;

  if (packed) {
    if (packed == PACKED_FORMAT_444)
      picWidth *= 3;
    else
      picWidth *= 2;

    chromaSize = 0;
  }

  lumaSize = picWidth * nY;

  size = lumaSize + chromaSize * 2;

  lumaSize *= bytesPerPixel;
  chromaSize *= bytesPerPixel;
  size *= bytesPerPixel;
  picWidth *= bytesPerPixel;
  chromaWidth *= bytesPerPixel;

  if (interLeave) {
    chromaSize = chromaSize * 2;
    chromaWidth = chromaWidth * 2;
    chromaStride_i = chromaStride_i;
  }

  if ((picWidth == stride) && (chromaWidth == chromaStride_i)) {
    memcpy(puc, addr, lumaSize);
    if (packed) return size;

    if (interLeave) {
      puc = dst + lumaSize;
      addr = addrCb;
      memcpy(puc, addr, chromaSize);
    } else {
      puc = dst + lumaSize;
      addr = addrCb;
      memcpy(puc, addr, chromaSize);

      puc = dst + lumaSize + chromaSize;
      addr = addrCr;
      memcpy(puc, addr, chromaSize);
    }
  } else {
    printf("nY=%d\n", nY);

    for (y = 0; y < nY; ++y) {
      memcpy(puc + y * picWidth, addr + stride * y, picWidth);
    }

    if (packed) return size;

    if (interLeave) {
      printf("nC=%d\n", chromaHeight / 2);
      puc = dst + lumaSize;
      addr = addrCb;
      for (y = 0; y < (chromaHeight / 2); ++y) {
        memcpy(puc + y * (chromaWidth), addr + (chromaStride_i)*y, chromaWidth);
      }
    } else {
      puc = dst + lumaSize;
      addr = addrCb;
      for (y = 0; y < nCb; ++y) {
        memcpy(puc + y * chromaWidth, addr + chromaStride_i * y, chromaWidth);
      }
      puc = dst + lumaSize + chromaSize;
      addr = addrCr;
      for (y = 0; y < nCr; ++y) {
        memcpy(puc + y * chromaWidth, addr + chromaStride_i * y, chromaWidth);
      }
    }
  }
  return size;
}

void GetMcuUnitSize(int format, int* mcuWidth, int* mcuHeight) {
  switch (format) {
    case FORMAT_420:
      *mcuWidth = 16;
      *mcuHeight = 16;
      break;
    case FORMAT_422:
      *mcuWidth = 16;
      *mcuHeight = 8;
      break;
    case FORMAT_440:
      *mcuWidth = 8;
      *mcuHeight = 16;
      break;
    default:  // FORMAT_444,400
      *mcuWidth = 8;
      *mcuHeight = 8;
      break;
  }
}

int GetFrameBufSize(int framebufFormat, int picWidth, int picHeight) {
  int framebufSize = 0;
  int framebufWidth, framebufHeight;
  framebufWidth = picWidth;
  framebufHeight = picHeight;

  switch (framebufFormat) {
    case FORMAT_420:
      framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                     ((framebufWidth + 1) / 2) * ((framebufHeight + 1) / 2) * 2;
      break;
    case FORMAT_440:
      framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                     framebufWidth * ((framebufHeight + 1) / 2) * 2;
      break;
    case FORMAT_422:
      framebufSize = framebufWidth * framebufHeight +
                     ((framebufWidth + 1) / 2) * framebufHeight * 2;
      break;
    case FORMAT_444:
      framebufSize = framebufWidth * framebufHeight * 3;
      break;
    case FORMAT_400:
      framebufSize = framebufWidth * framebufHeight;
      break;
  }

  framebufSize = ((framebufSize + 7) & ~7);

  return framebufSize;
}

STATIC void GetFrameBufStride(FrameFormat subsample, CbCrInterLeave cbcrIntlv,
                              PackedFormat packed, BOOL scalerOn, Uint32 width,
                              Uint32 height, Uint32 bytePerPixel,
                              Uint32* oLumaStride, Uint32* oLumaHeight,
                              Uint32* oChromaStride, Uint32* oChromaHeight) {
  Uint32 lStride, cStride;
  Uint32 lHeight, cHeight;
  lStride = JPU_CEIL(8, width);
  lHeight = height;
  cHeight = height / 2;

  if (packed == PACKED_FORMAT_NONE) {
    Uint32 chromaDouble = (cbcrIntlv == CBCR_SEPARATED) ? 1 : 2;

    switch (subsample) {
      case FORMAT_400:
        cStride = 0;
        cHeight = 0;
        break;
      case FORMAT_420:
        cStride = (lStride / 2) * chromaDouble;
        cHeight = height / 2;
        break;
      case FORMAT_422:
        cStride = (lStride / 2) * chromaDouble;
        cHeight = height;
        break;
      case FORMAT_440:
        cStride = lStride * chromaDouble;
        cHeight = height / 2;
        break;
      case FORMAT_444:
        cStride = lStride * chromaDouble;
        cHeight = height;
        break;
      default:
        cStride = 0;
        lStride = 0;
        cHeight = 0;
        break;
    }
  } else {
    switch (packed) {
      case PACKED_FORMAT_422_YUYV:
      case PACKED_FORMAT_422_UYVY:
      case PACKED_FORMAT_422_YVYU:
      case PACKED_FORMAT_422_VYUY:
        lStride = lStride * 2;
        cStride = 0;
        cHeight = 0;
        break;
      case PACKED_FORMAT_444:
        lStride = lStride * 3;
        cStride = 0;
        cHeight = 0;
        break;
      default:
        lStride = 0;
        cStride = 0;
        break;
    }
  }

  if (scalerOn == TRUE) {
    /* Luma stride */
    if (subsample == FORMAT_420 || subsample == FORMAT_422 ||
        (PACKED_FORMAT_422_YUYV <= packed &&
         packed <= PACKED_FORMAT_422_VYUY)) {
      lStride = JPU_CEIL(32, lStride);
    } else {
      lStride = JPU_CEIL(16, lStride);
    }
    /* Chroma stride */
    if (cbcrIntlv == CBCR_SEPARATED) {
      if (subsample == FORMAT_444) {
        cStride = JPU_CEIL(16, cStride);
      } else {
        cStride = JPU_CEIL(8, cStride);
      }
    } else {
      cStride = JPU_CEIL(32, cStride);
    }
  } else {
    lStride = JPU_CEIL(8, lStride);
    if (subsample == FORMAT_420 || subsample == FORMAT_422) {
      cStride = JPU_CEIL(16, cStride);
    } else {
      cStride = JPU_CEIL(8, cStride);
    }
  }
  lHeight = JPU_CEIL(8, lHeight);
  cHeight = JPU_CEIL(8, cHeight);

  lStride *= bytePerPixel;
  cStride *= bytePerPixel;
  // cStride = lStride;
  if (oLumaStride) *oLumaStride = lStride;
  if (oLumaHeight) *oLumaHeight = lHeight;
  if (oChromaStride) *oChromaStride = cStride;
  if (oChromaHeight) *oChromaHeight = cHeight;
}

int GetDPBBufSize(int framebufFormat, int picWidth, int picHeight,
                  int picWidth_C, int interleave) {
  int framebufSize = 0;
  int framebufWidth, framebufHeight, framebufWidth_C;
  framebufWidth = picWidth;
  framebufHeight = picHeight;
  framebufWidth_C = picWidth_C;

  switch (framebufFormat) {
    case FORMAT_420:
      if (interleave == 0)
        framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                       framebufWidth_C * ((framebufHeight + 1) / 2) * 2;
      else
        framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                       framebufWidth_C * ((framebufHeight + 1) / 2);
      break;
    case FORMAT_440:
      if (interleave == 0)
        framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                       framebufWidth_C * ((framebufHeight + 1) / 2) * 2 * 2;
      else
        framebufSize = framebufWidth * ((framebufHeight + 1) / 2 * 2) +
                       framebufWidth_C * ((framebufHeight + 1) / 2) * 2;
      break;
    case FORMAT_422:
      if (interleave == 0)
        framebufSize = framebufWidth * framebufHeight +
                       framebufWidth_C * framebufHeight * 2;
      else
        framebufSize =
            framebufWidth * framebufHeight + framebufWidth_C * framebufHeight;
      break;
    case FORMAT_444:
      framebufSize = framebufWidth * framebufHeight * 3;
      break;
    case FORMAT_400:
      framebufSize = framebufWidth * framebufHeight;
      if (interleave != 0)
        printf("Warning: 400 does not have interleave mode ! \n");
      break;
  }

  framebufSize = ((framebufSize + 7) & ~7);

  return framebufSize;
}
FrameBufferInfo* AllocateFrameBuffer(BufferAllocator* bufferAllocator,
                                     Uint32 instIdx, FrameFormat subsample,
                                     CbCrInterLeave cbcrIntlv,
                                     PackedFormat packed, Uint32 rotation,
                                     BOOL scalerOn, Uint32 width, Uint32 height,
                                     Uint32 bitDepth) {
  FrameBufferInfo* frameBufferInfo = NULL;
  Uint32 fbLumaStride, fbLumaHeight, fbChromaStride, fbChromaHeight;
  Uint32 fbLumaSize, fbChromaSize, fbSize;
  Uint32 i;
  Uint32 bytePerPixel = (bitDepth + 7) / 8;

  if (rotation == 90 || rotation == 270) {
    if (subsample == FORMAT_422)
      subsample = FORMAT_440;
    else if (subsample == FORMAT_440)
      subsample = FORMAT_422;
  }
  GetFrameBufStride(subsample, cbcrIntlv, packed, scalerOn, width, height,
                    bytePerPixel, &fbLumaStride, &fbLumaHeight, &fbChromaStride,
                    &fbChromaHeight);

  fbLumaSize = fbLumaStride * fbLumaHeight;
  fbChromaSize = fbChromaStride * fbChromaHeight;

  if (cbcrIntlv == CBCR_SEPARATED) {
    /* fbChromaSize MUST be zero when format is packed mode */
    fbSize = fbLumaSize + 2 * fbChromaSize;

  } else {
    /* Semi-planar */
    fbSize = fbLumaSize + fbChromaSize;
  }
  frameBufferInfo = (FrameBufferInfo*)malloc(sizeof(FrameBufferInfo));
  frameBufferInfo->dmaBuffer.size = fbSize;
  frameBufferInfo->format = subsample;
  frameBufferInfo->stride = fbLumaStride;
  frameBufferInfo->strideC = fbChromaStride;
  frameBufferInfo->yOffset = 0;
  if (fbChromaSize) {
    frameBufferInfo->uOffset = fbLumaSize;
    frameBufferInfo->vOffset =
        (cbcrIntlv == CBCR_SEPARATED) ? fbLumaSize + fbChromaSize : 0;
  }
  frameBufferInfo->dmaBuffer.fd = DmabufHeapAllocSystem(
      bufferAllocator, true, frameBufferInfo->dmaBuffer.size, 0, 0);
  if (frameBufferInfo->dmaBuffer.fd < 0) {
    return NULL;
  }
  return frameBufferInfo;
}

void FreeFrameBuffer(FrameBufferInfo* frameBufferInfo) {
  if (!frameBufferInfo) {
    JLOG(ERR, "%s:%d frameBufferInfo is NULL\n", __FUNCTION__, __LINE__);
    return;
  }
  if (frameBufferInfo->dmaBuffer.fd >= 0) close(frameBufferInfo->dmaBuffer.fd);
  free(frameBufferInfo);
}

BOOL ParseDecTestLongArgs(void* config, const char* argName, char* value) {
  BOOL ret = TRUE;
  DecConfigParam* dec = (DecConfigParam*)config;
  dec->loop_count = 1;

  if (strcmp(argName, "output") == 0) {
    strcpy(dec->yuvFileName, value);
  } else if (strcmp(argName, "input") == 0) {
    strcpy(dec->bitstreamFileName, value);
  } else if (strcmp(argName, "stream-endian") == 0) {
    dec->StreamEndian = atoi(value);
  } else if (strcmp(argName, "frame-endian") == 0) {
    dec->FrameEndian = atoi(value);
  } else if (strcmp(argName, "bs-size") == 0) {
    dec->bsSize = atoi(value);
    if (dec->bsSize == 0) {
      JLOG(ERR, "bitstream buffer size is 0\n");
      ret = FALSE;
    }
  } else if (strcmp(argName, "roi") == 0) {
    char* val;
    val = strtok(value, ",");
    if (val == NULL) {
      JLOG(ERR, "Invalid ROI option: %s\n", value);
      ret = FALSE;
    } else {
      dec->roiOffsetX = atoi(val);
    }
    val = strtok(NULL, ",");
    if (val == NULL) {
      JLOG(ERR, "Invalid ROI option: %s\n", value);
      ret = FALSE;
    } else {
      dec->roiOffsetY = atoi(val);
    }
    val = strtok(NULL, ",");
    if (val == NULL) {
      JLOG(ERR, "Invalid ROI option: %s\n", value);
      ret = FALSE;
    } else {
      dec->roiWidth = atoi(val);
    }
    val = strtok(NULL, ",");
    if (val == NULL) {
      JLOG(ERR, "Invalid ROI option: %s\n", value);
      ret = FALSE;
    } else {
      dec->roiHeight = atoi(val);
    }
    dec->roiEnable = TRUE;
  } else if (strcmp(argName, "subsample") == 0) {
    if (strcasecmp(value, "none") == 0) {
      dec->subsample = FORMAT_MAX;
    } else if (strcasecmp(value, "420") == 0) {
      dec->subsample = FORMAT_420;
    } else if (strcasecmp(value, "422") == 0) {
      dec->subsample = FORMAT_422;
    } else if (strcasecmp(value, "444") == 0) {
      dec->subsample = FORMAT_444;
    } else {
      JLOG(ERR, "Not supported sub-sample: %s\n", value);
      ret = FALSE;
    }
  } else if (strcmp(argName, "ordering") == 0) {
    if (strcasecmp(value, "none") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_NONE;
    } else if (strcasecmp(value, "nv12") == 0) {
      dec->cbcrInterleave = CBCR_INTERLEAVE;
      dec->packedFormat = PACKED_FORMAT_NONE;
    } else if (strcasecmp(value, "nv21") == 0) {
      dec->cbcrInterleave = CRCB_INTERLEAVE;
      dec->packedFormat = PACKED_FORMAT_NONE;
    } else if (strcasecmp(value, "yuyv") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_422_YUYV;
    } else if (strcasecmp(value, "uyvy") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_422_UYVY;
    } else if (strcasecmp(value, "yvyu") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_422_YVYU;
    } else if (strcasecmp(value, "vyuy") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_422_VYUY;
    } else if (strcasecmp(value, "ayuv") == 0) {
      dec->cbcrInterleave = CBCR_SEPARATED;
      dec->packedFormat = PACKED_FORMAT_444;
    } else {
      JLOG(ERR, "Not supported ordering: %s\n", value);
      ret = FALSE;
    }
  } else if (strcmp(argName, "rotation") == 0) {
    dec->rotation = atoi(value);
  } else if (strcmp(argName, "mirror") == 0) {
    dec->mirror = (JpgMirrorDirection)atoi(value);
  } else if (strcmp(argName, "scaleH") == 0) {
    dec->iHorScaleMode = atoi(value);
  } else if (strcmp(argName, "scaleV") == 0) {
    dec->iVerScaleMode = atoi(value);
  } else if (strcmp(argName, "profiling") == 0) {
    dec->profiling = atoi(value);
  } else if (strcmp(argName, "loop_count") == 0) {
    dec->loop_count = atoi(value);
  } else {
    JLOG(ERR, "Not defined option: %s\n", argName);
    ret = FALSE;
  }

  return ret;
}

BOOL ParseEncTestLongArgs(void* config, const char* argName, char* value) {
  BOOL ret = TRUE;
  EncConfigParam* enc = (EncConfigParam*)config;
  enc->loop_count = 1;
  if (strcmp(argName, "output") == 0) {
    strcpy(enc->bitstreamFileName, value);
  } else if (strcmp(argName, "input") == 0) {
    strcpy(enc->cfgFileName, value);
  } else if (strcmp(argName, "12bit") == 0) {
    enc->extendedSequential = TRUE;
  } else if (strcmp(argName, "cfg-dir") == 0) {
    if (strlen(value) > MAX_FILE_PATH) {
      JLOG(ERR, "cfg-dir patch%s is too long !!!\n", argName);
      return FALSE;
    } else {
      strncpy(enc->strCfgDir, value, MAX_FILE_PATH);
    }

  } else if (strcmp(argName, "yuv-dir") == 0) {
    if (strlen(value) > MAX_FILE_PATH) {
      JLOG(ERR, "yuv-dir patch%s is too long !!!\n", argName);
      return FALSE;
    } else {
      strncpy(enc->strYuvDir, value, MAX_FILE_PATH);
    }

  } else if (strcmp(argName, "stream-endian") == 0) {
    enc->StreamEndian = atoi(value);
  } else if (strcmp(argName, "frame-endian") == 0) {
    enc->FrameEndian = atoi(value);
  } else if (strcmp(argName, "bs-size") == 0) {
    enc->bsSize = atoi(value);
    if (enc->bsSize == 0) {
      JLOG(ERR, "bitstream buffer size is 0\n");
      ret = FALSE;
    }
  } else if (strcmp(argName, "quality") == 0) {
    enc->encQualityPercentage = atoi(value);
    if (enc->encQualityPercentage > 100) {
      JLOG(ERR, "Invalid quality factor: %d\n", enc->encQualityPercentage);
      ret = FALSE;
    }
  } else if (strcmp(argName, "enable-tiledMode") == 0) {
    enc->tiledModeEnable = (BOOL)atoi(value);
  } else if (strcmp(argName, "slice-height") == 0) {
    enc->sliceHeight = atoi(value);
  } else if (strcmp(argName, "enable-slice-intr") == 0) {
    enc->sliceInterruptEnable = atoi(value);
  } else if (strcmp(argName, "rotation") == 0) {
    enc->rotation = atoi(value);
  } else if (strcmp(argName, "mirror") == 0) {
    enc->mirror = (JpgMirrorDirection)atoi(value);
  } else if (strcmp(argName, "profiling") == 0) {
    enc->profiling = atoi(value);
  } else if (strcmp(argName, "loop_count") == 0) {
    enc->loop_count = atoi(value);
  } else {
    JLOG(ERR, "Not defined option: %s\n", argName);
    ret = FALSE;
  }

  return ret;
}

char* GetFileExtension(const char* filename) {
  Int32 len;
  Int32 i;

  len = strlen(filename);
  for (i = len - 1; i >= 0; i--) {
    if (filename[i] == '.') {
      return (char*)&filename[i + 1];
    }
  }

  return NULL;
}
