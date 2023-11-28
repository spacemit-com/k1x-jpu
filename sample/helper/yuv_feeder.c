/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include "yuv_feeder.h"

#include <assert.h>
#include <errno.h>
#include <linux/dma-buf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "jpulog.h"
#include "platform.h"
#define FB_QUEUE_SIZE 10

typedef struct {
  FILE* fp;
  Uint32 frameSize;
  Uint32 lumaSize;
  Uint32 chromaSize;
  Uint32 lumaLineWidth;
  Uint32 lumaHeight;
  Uint32 chromaLineWidth;
  Uint32 chromaHeight;
  Uint8* pYuv;
  Uint32 fbEndian;
  Uint32 currentRow;
  JdiDeviceCtx devctx;
} IYuvContext;

static void DefaultListener(YuvFeederListenerArg* arg) {
  UNREFERENCED_PARAMETER(arg);
}

static void CalcYuvFrameSize(YuvAttr* attr, IYuvContext* ctx) {
  Uint32 lSize = 0, cSize = 0, divc = 1;
  Uint32 Bpp = (attr->bpp + 7) >> 3;
  Uint32 divw, divh;

  divw = divh = 1;
  switch (attr->format) {
    case FORMAT_400:
      /* No chroma data */
      divw = divh = 0;
      break;
    case FORMAT_420:
      divw = 2;
      divh = 2;
      break;
    case FORMAT_422:
      divw = 2;
      divh = 1;
      break;
    case FORMAT_440:
      divw = 1;
      divh = 2;
      break;
    case FORMAT_444:
      break;
    default:
      JLOG(WARN, "%s:%d NOT SUPPORTED YUV FORMAT: %d\n", __FUNCTION__, __LINE__,
           attr->format);
      break;
  }
  divc = divw * divh;
  lSize = attr->width * attr->height * Bpp;
  cSize = divc == 0 ? 0 : lSize / divc;
  ctx->frameSize = lSize + 2 * cSize;
  ctx->lumaSize = lSize;
  ctx->chromaSize = cSize;
  ctx->lumaLineWidth = attr->width * Bpp;
  ctx->lumaHeight = attr->height;
  ctx->chromaLineWidth = divw == 0 ? 0 : attr->width * Bpp / divw;
  ctx->chromaHeight = divh == 0 ? 0 : attr->height / divh;

  if (attr->packedFormat != PACKED_FORMAT_NONE) {
    if (attr->packedFormat == PACKED_FORMAT_444) {
      ctx->lumaLineWidth *= 3;
    } else {
      ctx->lumaLineWidth *= 2;
    }
    ctx->chromaLineWidth = 0;
    ctx->chromaHeight = 0;
  } else {
    if (attr->chromaInterleaved != CBCR_SEPARATED) {
      ctx->chromaLineWidth *= 2;
    }
  }
}

static void CopyYuvData(JdiDeviceCtx devctx, void* fbAddr, Uint32 fbStride,
                        Uint8* data, Uint32 dataStride, Uint32 dataHeight,
                        Uint32 endian) {
  Uint8* addr = fbAddr;
  Uint8* pData = data;
  Uint32 i;

  for (i = 0; i < dataHeight; i++) {
    jdi_write_memory(devctx, addr, pData, dataStride, endian);
    pData += dataStride;
    addr += fbStride;
  }
}

/* @return  height of picture on success, -1 on failure
 */
static Int32 LoadFrameFromFile(IYuvContext* ctx, FrameBufferInfo* fb,
                               YuvAttr attr, Uint32 endian,
                               BufferAllocator* bufferAllocator) {
  Uint32 nread;
  BOOL success = TRUE;
  Uint8* pY;
  Uint8* pU;
  Uint8* pV;
  Uint8* pFrameBuffer;
  Uint32 fbLumaStride, fbLumaHeight, fbChromaStride, fbChromaHeight;
  Uint32 fbLumaSize, fbChromaSize, fbSize;
  Uint32 framebufWidth = (fb->format == FORMAT_420 || fb->format == FORMAT_422)
                             ? JPU_CEIL(16, attr.width)
                             : JPU_CEIL(8, attr.width);
  Uint32 framebufHeight = (fb->format == FORMAT_420 || fb->format == FORMAT_440)
                              ? JPU_CEIL(16, attr.height)
                              : JPU_CEIL(8, attr.height);
  // fbLumaStride = JPU_CEIL(8,framebufWidth);
  // fbChromaStride = JPU_CEIL(16,fbLumaStride/2);
  fbLumaStride = JPU_CEIL(16, framebufWidth);
  fbChromaStride = JPU_CEIL(16, fbLumaStride);
  fbLumaHeight = framebufHeight;
  fbChromaHeight = fbLumaHeight / 2;
  fbLumaSize = fbLumaStride * fbLumaHeight;
  fbChromaSize = fbChromaStride * fbChromaHeight;
  if ((nread = fread(ctx->pYuv, 1, ctx->frameSize, ctx->fp)) !=
      ctx->frameSize) {
    JLOG(WARN, "%s:%d INSUFFICIENT FRAME DATA!!!\n", __FUNCTION__, __LINE__);
    success = FALSE;
  }

  pY = ctx->pYuv;
  pFrameBuffer =
      (Uint8*)mmap(NULL, fbLumaSize + fbChromaSize, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fb->dmaBuffer.fd, 0);
  switch (attr.format) {
    case FORMAT_420:
      if (attr.packedFormat == PACKED_FORMAT_NONE) {
        CopyYuvData(ctx->devctx, pFrameBuffer, fb->stride, pY,
                    ctx->lumaLineWidth, ctx->lumaHeight, endian);
        pU = pY + ctx->lumaSize;
        if (attr.chromaInterleaved == CBCR_SEPARATED) {
          fbLumaHeight = framebufHeight;
          fbChromaHeight = fbLumaHeight / 2;
          fbLumaSize = fbLumaStride * fbLumaHeight;
          CopyYuvData(ctx->devctx, pFrameBuffer + fbLumaSize, fb->strideC, pU,
                      ctx->chromaLineWidth, ctx->chromaHeight, endian);
          pV = pU + ctx->chromaSize;
          CopyYuvData(ctx->devctx, pFrameBuffer + fbLumaSize + fbLumaSize / 4,
                      fb->strideC, pV, ctx->chromaLineWidth, ctx->chromaHeight,
                      endian);
        } else {
          fbLumaHeight = framebufHeight;
          fbChromaHeight = fbLumaHeight / 2;
          fbLumaSize = fbLumaStride * fbLumaHeight;
          fbChromaSize = fbChromaStride * fbChromaHeight;
          CopyYuvData(ctx->devctx, pFrameBuffer + fbLumaSize, fb->strideC, pU,
                      ctx->chromaLineWidth, ctx->chromaHeight, endian);
        }
      } else {
        CopyYuvData(ctx->devctx, pFrameBuffer + fbLumaSize, fb->stride, pY,
                    ctx->lumaLineWidth, ctx->lumaHeight, endian);
      }
      break;
    default:
      JLOG(ERR, "%s:%d NOT SUPPORTED YUV FORMAT:%d\n", __FUNCTION__, __LINE__,
           attr.format);
      success = FALSE;
      break;
  }
  munmap(pFrameBuffer, fbLumaSize + fbChromaSize);
  return success == FALSE ? -1 : attr.height;
}

/* @return  It returns the num of rows so far.
 */
static Int32 LoadSliceFromFile(IYuvContext* ctx, FrameBufferInfo* fb,
                               YuvAttr attr, Uint32 endian,
                               BufferAllocator* bufferAllocator) {
  Uint32 nread;
  BOOL success = TRUE;
  Uint8* pY;
  Uint8* pU;
  Uint8* pV;
  Uint32 lumaOffset, chromaOffset;
  Uint8* pFrameBufferVir;
#if 1
  if (ctx->currentRow == attr.height) {
    ctx->currentRow = 0;
  }

  if (ctx->currentRow == 0) {
    if ((nread = fread(ctx->pYuv, 1, ctx->frameSize, ctx->fp)) !=
        ctx->frameSize) {
      JLOG(WARN, "%s:%d INSUFFICIENT FRAME DATA!!!\n", __FUNCTION__, __LINE__);
      success = FALSE;
    }
  }
  pFrameBufferVir =
      (Uint8*)mmap(NULL, fb->dmaBuffer.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fb->dmaBuffer.fd, 0);
  lumaOffset = ctx->currentRow * ctx->lumaLineWidth;
  pY = ctx->pYuv;
  switch (attr.format) {
    case FORMAT_400:
      memcpy(pFrameBufferVir + lumaOffset, pY + lumaOffset, ctx->lumaLineWidth);
      ctx->currentRow++;
      /* No chroma data */
      break;
    case FORMAT_420:
    case FORMAT_422:
    case FORMAT_440:
    case FORMAT_444:
      if (attr.packedFormat == PACKED_FORMAT_NONE) {
        Uint32 bytesPerPixel = (attr.format == FORMAT_422) ? 2 : 3;
        Uint32 offset = ctx->currentRow * ctx->lumaLineWidth * bytesPerPixel;
        Uint32 lineWidth = ctx->lumaLineWidth * bytesPerPixel;
        memcpy(pFrameBufferVir + fb->yOffset + offset, pY + offset, lineWidth);
        ctx->currentRow++;
      } else {
        Uint32 currentChromaRow =
            (attr.format == FORMAT_444) ? ctx->currentRow : ctx->currentRow / 2;

        memcpy(pFrameBufferVir + fb->yOffset + lumaOffset, pY + lumaOffset,
               ctx->lumaLineWidth);
        if (attr.format != FORMAT_444) {
          memcpy(
              pFrameBufferVir + fb->yOffset + lumaOffset + ctx->lumaLineWidth,
              pY + lumaOffset + ctx->lumaLineWidth, ctx->lumaLineWidth);
        }
        pU = pY + ctx->lumaSize;
        if (attr.chromaInterleaved == TRUE) {
          chromaOffset = currentChromaRow * ctx->chromaLineWidth * 2;
          memcpy(pFrameBufferVir + fb->uOffset + chromaOffset,
                 pU + chromaOffset, 2 * ctx->chromaLineWidth);
        } else {
          chromaOffset = currentChromaRow * ctx->chromaLineWidth;
          memcpy(pFrameBufferVir + fb->uOffset + chromaOffset,
                 pU + chromaOffset, ctx->chromaLineWidth);
          pV = pU + ctx->chromaSize;
          memcpy(pFrameBufferVir + fb->vOffset + chromaOffset,
                 pV + chromaOffset, ctx->chromaLineWidth);
        }
        ctx->currentRow += attr.format == FORMAT_444 ? 1 : 2;
      }
      break;
    default:
      JLOG(ERR, "%s:%d NOT SUPPORTED YUV YUV_FMT:%d\n", __FUNCTION__, __LINE__);
      success = FALSE;
      break;
  }
#endif
  return success == FALSE ? -1 : ctx->currentRow;
}

static BOOL IYuvFeeder_Create(AbstractYuvFeeder* feeder, const char* path,
                              JdiDeviceCtx devctx) {
  IYuvContext* ctx;
  FILE* fp;

  if ((fp = fopen(path, "rb")) == NULL) {
    JLOG(ERR, "%s:%d failed to open yuv file: %s\n", __FUNCTION__, __LINE__,
         path);
    return FALSE;
  }

  if ((ctx = (IYuvContext*)malloc(sizeof(IYuvContext))) == NULL) {
    fclose(fp);
    return FALSE;
  }

  memset(ctx, 0, sizeof(IYuvContext));

  ctx->fp = fp;
  ctx->devctx = devctx;
  CalcYuvFrameSize(&feeder->attr, ctx);
  ctx->pYuv = malloc(ctx->frameSize);
  feeder->impl->context = (void*)ctx;

  return TRUE;
}

static BOOL IYuvFeeder_Destroy(AbstractYuvFeeder* feeder) {
  IYuvContext* ctx;

  if (feeder == NULL) {
    return FALSE;
  }
  ctx = (IYuvContext*)feeder->impl->context;
  if (ctx->fp != NULL) {
    fclose(ctx->fp);  // lint !e482
  }
  if (ctx->pYuv != NULL) {
    free(ctx->pYuv);
  }
  free(ctx);

  return FALSE;
}

static Int32 IYuvFeeder_Feed(AbstractYuvFeeder* feeder, FrameBufferInfo* fb,
                             Uint32 endian, BufferAllocator* bufferAllocator) {
  if (feeder->sliceHeight > 0) {
    return LoadSliceFromFile((IYuvContext*)feeder->impl->context, fb,
                             feeder->attr, endian, bufferAllocator);
  } else {
    return LoadFrameFromFile((IYuvContext*)feeder->impl->context, fb,
                             feeder->attr, endian, bufferAllocator);
  }
}

static BOOL IYuvFeeder_Configure(AbstractYuvFeeder* feeder, Uint32 cmd,
                                 void* arg) {
  UNREFERENCED_PARAMETER(feeder);
  UNREFERENCED_PARAMETER(cmd);
  UNREFERENCED_PARAMETER(arg);

  return FALSE;
}

static YuvFeederImpl IYuvFeederImpl = {NULL, IYuvFeeder_Create, IYuvFeeder_Feed,
                                       IYuvFeeder_Destroy,
                                       IYuvFeeder_Configure};

static Int32 FeedYuv(AbstractYuvFeeder* feeder, FrameBufferInfo* fb,
                     BufferAllocator* bufferAllocator) {
  YuvFeederImpl* impl = feeder->impl;

  return impl->Feed(feeder, fb, feeder->fbEndian, bufferAllocator);
}

static void NotifyPictureDone(AbstractYuvFeeder* absFeeder, Int32 lines) {
  YuvFeederListenerArg larg;
  BOOL doNotify = FALSE;

  // Notify to the client when errors occur or slice buffer is filled.
  doNotify = (BOOL)(lines <= 0 || (lines % (Int32)absFeeder->sliceHeight) == 0);

  if (doNotify) {
    larg.currentRow = lines <= 0 ? absFeeder->attr.height : lines;
    larg.context = absFeeder->sliceNotiCtx;
    larg.height = absFeeder->attr.height;
    absFeeder->listener(&larg);
  }

  return;
}

static void YuvFeederThread(void* arg) {
  AbstractYuvFeeder* absFeeder = (AbstractYuvFeeder*)arg;
  FrameBufferInfo* fb;
  Int32 lines;
  BOOL done = FALSE;

  while (absFeeder->threadStop == FALSE) {
    if ((fb = Queue_Dequeue(absFeeder->fbQueue)) == NULL) {
      MSleep(10);
      continue;
    }

    done = FALSE;
    while (done == FALSE) {
      if ((lines = FeedYuv(absFeeder, fb, NULL)) == absFeeder->attr.height) {
        done = TRUE;
      }
      NotifyPictureDone(absFeeder, lines);
      if (lines <= 0) break;  // Error
    }
  }
}

/*lint -esym(438, ap) */
YuvFeeder YuvFeeder_Create(YuvFeederMode mode, const char* path, YuvAttr attr,
                           Uint32 fbEndian, YuvFeederListener listener,
                           JdiDeviceCtx devctx) {
  AbstractYuvFeeder* feeder;
  YuvFeederImpl* impl;
  BOOL success = FALSE;

  if (path == NULL) {
    JLOG(ERR, "%s:%d src path is NULL\n", __FUNCTION__, __LINE__);
    return NULL;
  }

  if ((impl = malloc(sizeof(YuvFeederImpl))) == NULL) {
    return NULL;
  }
  memcpy((void*)impl, (void*)&IYuvFeederImpl, sizeof(YuvFeederImpl));

  if ((feeder = (AbstractYuvFeeder*)calloc(1, sizeof(AbstractYuvFeeder))) ==
      NULL) {
    free(impl);
    return NULL;
  }
  feeder->impl = impl;
  feeder->thread = NULL;
  feeder->threadStop = FALSE;
  feeder->listener = NULL;
  feeder->fbEndian = fbEndian;
  feeder->fbQueue = NULL;
  feeder->attr = attr;

  success = impl->Create(feeder, path, devctx);

  if (success == FALSE) return NULL;

  if (mode == YUV_FEEDER_MODE_THREAD) {
    feeder->thread = (void*)JpuThread_Create((JpuThreadRunner)YuvFeederThread,
                                             (void*)feeder);
    feeder->listener = (listener == NULL) ? DefaultListener : listener;
    feeder->fbQueue =
        Queue_Create_With_Lock(FB_QUEUE_SIZE, sizeof(FrameBufferInfo));
  }

  return feeder;
}
/*lint +esym(438, ap) */

BOOL YuvFeeder_Destroy(YuvFeeder feeder) {
  YuvFeederImpl* impl = NULL;
  AbstractYuvFeeder* absFeeder = (AbstractYuvFeeder*)feeder;

  if (absFeeder == NULL) {
    JLOG(ERR, "%s:%d Invalid handle\n", __FUNCTION__, __LINE__);
    return FALSE;
  }

  impl = absFeeder->impl;

  if (absFeeder->thread) {
    absFeeder->threadStop = TRUE;
    JpuThread_Join((JpuThread)absFeeder->thread);
  }

  impl->Destroy(absFeeder);
  free(impl);
  free(feeder);

  return TRUE;
}

BOOL YuvFeeder_Feed(YuvFeeder feeder, FrameBufferInfo* fb,
                    BufferAllocator* bufferAllocator) {
  AbstractYuvFeeder* absFeeder = (AbstractYuvFeeder*)feeder;

  if (absFeeder == NULL) {
    JLOG(ERR, "%s:%d Invalid handle\n", __FUNCTION__, __LINE__);
    return FALSE;
  }

  if (absFeeder->thread == NULL) {
    return FeedYuv(absFeeder, fb, bufferAllocator);
  } else {
    return Queue_Enqueue(absFeeder->fbQueue, fb);
  }
}

BOOL YuvFeeder_Configure(YuvFeeder feeder, YuvFeederCmd cmd, void* arg) {
  AbstractYuvFeeder* absFeeder = (AbstractYuvFeeder*)feeder;
  BOOL success = TRUE;
  SliceNotifyParam* sliceParam;

  if (absFeeder == NULL) {
    JLOG(ERR, "%s:%d Invalid handle\n", __FUNCTION__, __LINE__);
    return FALSE;
  }

  switch (cmd) {
    case YUV_FEEDER_CMD_SET_SLICE_NOTIFY:
      if (absFeeder->thread == NULL) {
        JLOG(ERR, "The YuvFeeder is not thread mode\n");
        break;
      }
      sliceParam = (SliceNotifyParam*)arg;
      absFeeder->sliceHeight = sliceParam->rows;
      absFeeder->sliceNotiCtx = sliceParam->arg;
      break;
    default:
      success = FALSE;
      break;
  }

  return success;
}
