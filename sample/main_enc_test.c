/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include <errno.h>
#include <getopt.h>
#include <linux/dma-buf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "BufferAllocatorWrapper.h"
#include "jpuencapi.h"
#include "jpulog.h"
#include "main_helper.h"

#define NUM_FRAME_BUF MAX_FRAME
#define EXTRA_FRAME_BUFFER_NUM 0
#define ENC_SRC_BUF_NUM 1
#define CFG_DIR "./cfg"
#define YUV_DIR "./yuv"
#define BS_SIZE_ALIGNMENT 1024
#define MIN_BS_SIZE 1024
#define DEFAULT_OUTPUT_PATH "output.jpg"

static void GetSourceYuvAttributes(EncOpenParam encOP, YuvAttr* retAttr) {
  retAttr->bigEndian = TRUE;
  retAttr->bpp = encOP.jpg12bit == TRUE ? 12 : 8;
  retAttr->chromaInterleaved = encOP.chromaInterleave;
  retAttr->packedFormat = encOP.packedFormat;
  if (encOP.packedFormat == PACKED_FORMAT_NONE) {
    retAttr->format = encOP.sourceFormat;
  } else {
    switch (encOP.packedFormat) {
      case PACKED_FORMAT_422_YUYV:
      case PACKED_FORMAT_422_UYVY:
      case PACKED_FORMAT_422_YVYU:
      case PACKED_FORMAT_422_VYUY:
        retAttr->format = FORMAT_422;
        break;
      case PACKED_FORMAT_444:
        retAttr->format = FORMAT_444;
        break;
      default:
        retAttr->format = FORMAT_MAX;
        break;
    }
  }
  retAttr->width = encOP.picWidth;
  retAttr->height = encOP.picHeight;

  return;
}

static void CalcSliceHeight(EncOpenParam* encOP, Uint32 sliceHeight,
                            Uint32 rotation, int* restartInterval,
                            BOOL* sliceInstMode) {
  Uint32 mSliceHeight;
  Uint32 width =
      (rotation == 90 || rotation == 270) ? encOP->picHeight : encOP->picWidth;
  Uint32 height =
      (rotation == 90 || rotation == 270) ? encOP->picWidth : encOP->picHeight;
  FrameFormat format = encOP->sourceFormat;

  if (rotation == 90 || rotation == 270) {
    if (format == FORMAT_422)
      format = FORMAT_440;
    else if (format == FORMAT_440)
      format = FORMAT_422;
  }

  mSliceHeight = (sliceHeight == 0) ? height : sliceHeight;

  if (mSliceHeight != height) {
    if (format == FORMAT_420 || format == FORMAT_422)
      *restartInterval = width / 16;
    else
      *restartInterval = width / 8;

    if (format == FORMAT_420 || format == FORMAT_440)
      *restartInterval *= (mSliceHeight / 16);
    else
      *restartInterval *= (mSliceHeight / 8);
    *sliceInstMode = TRUE;
  }
}

static void Help(const char* programName) {
  JLOG(INFO,
       "-----------------------------------------------------------------------"
       "-------\n");
  JLOG(INFO, " JPU Encoder \n");
  JLOG(INFO,
       "-----------------------------------------------------------------------"
       "-------\n");
  JLOG(INFO, "%s [option] cfg_file \n", programName);
  JLOG(INFO, "-h                      help\n");
  JLOG(INFO, "--output=FILE           output file path\n");
  JLOG(
      INFO,
      "--cfg-dir=DIR           folder that has encode parameters default: %s\n",
      CFG_DIR);
  JLOG(INFO,
       "--yuv-dir=DIR           folder that has an input source image. "
       "default: %s\n",
       YUV_DIR);
  JLOG(INFO,
       "--yuv=FILE              use given yuv file instead of yuv file in cfg "
       "file\n");
  // JLOG(INFO, "--slice-height=height   the vertical height of a slice.
  // multiple of 8 alignment. 0 is to set the height of picture\n"); JLOG(INFO,
  // "--enable-slice-intr     enable get the interrupt at every slice
  // encoded\n"); JLOG(INFO, "--stream-endian=ENDIAN  bitstream endianness.
  // refer to datasheet Chapter 4.\n"); JLOG(INFO, "--frame-endian=ENDIAN pixel
  // endianness of 16bit input source. refer to datasheet Chapter 4.\n");
  JLOG(INFO, "--bs-size=SIZE          bitstream buffer size in byte\n");
  JLOG(INFO, "--quality=PERCENTAGE    quality factor(1..100)\n");
  // JLOG(INFO, "--enable-tiledMode      enable tiled mode (default linear
  // mode)\n");

  exit(1);
}

/* @brief   Test jpeg encoder
 * @return  0 for success, 1 for failure
 */

BOOL TestEncoder(EncConfigParam* param) {
  void* handle = NULL;
  EncOpenParam encOP = {0};
  FrameBufferInfo* frameBuffer;
  void* jpegImageVirtAddr;
  ImageBufferInfo jpegImageBuffer;
  JpgRet ret = JPG_RET_SUCCESS;
  Uint32 frameIdx = 0;
  int srcFrameFormat = 0;
  int framebufWidth = 0, framebufHeight = 0;
  BOOL suc = FALSE;
  EncConfigParam encConfig;
  BOOL boolVal = FALSE;
  YuvFeeder yuvFeeder = NULL;
  YuvAttr sourceAttr;
  char yuvPath[MAX_FILE_PATH];
  Uint32 bitDepth = 8;
  BSWriter writer = NULL;
  Uint32 esSize = 0;
  JdiDeviceCtx devctx = NULL;
  struct timeval start_time;
  struct timeval end_time;
  double total_time = 0;
  Uint32 profiling = 0;
  Uint32 loop_count = 1;
  BufferAllocator* bufferAllocator = NULL;

  encConfig = *param;
  if (strlen(encConfig.cfgFileName) != 0) {
    boolVal = GetJpgEncOpenParam(&encOP, &encConfig);
    profiling = encConfig.profiling;
    loop_count = encConfig.loop_count;
    JLOG(INFO, "### profiling:%d loop count: %d ###\n", profiling, loop_count);
  }
  if (boolVal == FALSE) {
    suc = FALSE;
    goto ERR_ENC;
  }
  while (loop_count) {
    bufferAllocator = CreateDmabufHeapBufferAllocator();
    ret = AsrJpuEncOpen(&handle, &encOP);
    if (ret != JPG_RET_SUCCESS && ret != JPG_RET_CALLED_BEFORE) {
      JLOG(ERR, "JPU_Init failed Error code is 0x%x \n", ret);
      goto ERR_ENC;
    }
    if (handle == NULL) {
      JLOG(ERR, "jpu enc open failed !\n");
      return FALSE;
    }
    if (NULL ==
        (writer = BitstreamWriter_Create(encConfig.writerType, &encConfig,
                                         encConfig.bitstreamFileName))) {
      return FALSE;
    }
    if (encConfig.encQualityPercentage > 0) {
      JLOG(INFO, "JPU SET QualityPercentage:%d \n",
           encConfig.encQualityPercentage);
      AsrJpuEncSetParam(handle, JPU_QUALITY, &encConfig.encQualityPercentage);
    }
    if (encConfig.mirror != MIRDIR_NONE) {
      int mirror = encConfig.mirror;
      JLOG(INFO, "JPU SET JPU_MIRROR:%d \n", mirror);
      AsrJpuEncSetParam(handle, JPU_MIRROR, &mirror);
    }
    if (encConfig.rotation != 0) {
      int rotation = encConfig.rotation;
      AsrJpuEncSetParam(handle, JPU_ROTATION, &rotation);
    }
    if (encConfig.extendedSequential) {
      AsrJpuEncSetParam(handle, JPU_12BIT, &encConfig.extendedSequential);
    }
    if (encConfig.StreamEndian) {
      AsrJpuEncSetParam(handle, JPU_STREAM_ENDIAN, &encConfig.StreamEndian);
    }
    if (encConfig.FrameEndian) {
      AsrJpuEncSetParam(handle, JPU_FRAME_ENDIAN, &encConfig.FrameEndian);
    }
    if ((encConfig.bsSize % BS_SIZE_ALIGNMENT) != 0 ||
        encConfig.bsSize < MIN_BS_SIZE) {
      JLOG(ERR, "Invalid size of bitstream buffer\n");
      goto ERR_ENC;
    }

    jpegImageBuffer.dmaBuffer.size = encConfig.bsSize;
    jpegImageBuffer.dataOffset = 0;
    jpegImageBuffer.imageSize = 0;
    jpegImageBuffer.dmaBuffer.fd = DmabufHeapAllocSystem(
        bufferAllocator, true, jpegImageBuffer.dmaBuffer.size, 0, 0);
    if (jpegImageBuffer.dmaBuffer.fd < 0) {
      JLOG(ERR,
           "#### DmabufHeapAllocSystem alloc stream fd:%d size:%d is invaild "
           "####\n",
           jpegImageBuffer.dmaBuffer.fd, jpegImageBuffer.dmaBuffer.size);
      goto ERR_ENC;
    }

    // jpegImageVirtAddr =
    // mmap(NULL,jpegImageBuffer.dmaBuffer.size,PROT_READ|PROT_WRITE,MAP_SHARED,jpegImageBuffer.dmaBuffer.fd,0);
    if (encOP.packedFormat) {
      if (encOP.packedFormat == PACKED_FORMAT_444 &&
          encOP.sourceFormat != FORMAT_444) {
        JLOG(ERR,
             "Invalid operation mode : In case of using packed mode. "
             "sourceFormat must be FORMAT_444\n");
        goto ERR_ENC;
      }
    }

    // srcFrameFormat means that it is original source image format.
    srcFrameFormat = encOP.sourceFormat;
    framebufWidth =
        (srcFrameFormat == FORMAT_420 || srcFrameFormat == FORMAT_422)
            ? JPU_CEIL(16, encOP.picWidth)
            : JPU_CEIL(8, encOP.picWidth);
    framebufHeight =
        (srcFrameFormat == FORMAT_420 || srcFrameFormat == FORMAT_440)
            ? JPU_CEIL(16, encOP.picHeight)
            : JPU_CEIL(8, encOP.picHeight);
    bitDepth = (encOP.jpg12bit == FALSE) ? 8 : 12;

    frameBuffer = AllocateFrameBuffer(
        bufferAllocator, 0, encOP.sourceFormat, encConfig.chromaInterleave,
        encConfig.packedFormat, encConfig.rotation, 0, framebufWidth,
        framebufHeight, bitDepth);
    if (frameBuffer == NULL) {
      JLOG(ERR, "Failed to AllocateFrameBuffer()\n");
      goto ERR_ENC;
    }
    sprintf(yuvPath, "%s/%s", encConfig.strYuvDir, encConfig.yuvFileName);
    GetSourceYuvAttributes(encOP, &sourceAttr);
    if ((yuvFeeder =
             YuvFeeder_Create(YUV_FEEDER_MODE_NORMAL, yuvPath, sourceAttr,
                              JDI_LITTLE_ENDIAN, NULL, devctx)) == NULL) {
      goto ERR_ENC;
    }

    if (YuvFeeder_Feed(yuvFeeder, frameBuffer, bufferAllocator) == FALSE) {
      goto ERR_ENC;
    }
    if (profiling) gettimeofday(&start_time, 0);
    ret = AsrJpuEncStartOneFrame(handle, frameBuffer, &jpegImageBuffer);
    esSize = jpegImageBuffer.imageSize;
    if (ret != JPG_RET_SUCCESS) {
      JLOG(ERR, "JPU_EncStartOneFrame failed Error code is 0x%x \n", ret);
      goto ERR_ENC;
    }
    if (profiling) {
      gettimeofday(&end_time, 0);
      total_time += (end_time.tv_sec - start_time.tv_sec) * 1000.f +
                    (end_time.tv_usec - start_time.tv_usec) / 1000.f;
    }
    jpegImageVirtAddr =
        mmap(NULL, jpegImageBuffer.dmaBuffer.size, PROT_READ | PROT_WRITE,
             MAP_SHARED, jpegImageBuffer.dmaBuffer.fd, 0);
    if (FALSE ==
        BitstreamWriter_Act(writer, jpegImageVirtAddr + 0, esSize, FALSE)) {
      goto ERR_ENC;
    }
    munmap(jpegImageVirtAddr, jpegImageBuffer.dmaBuffer.size);
    close(jpegImageBuffer.dmaBuffer.fd);
    FreeFrameBuffer(frameBuffer);
    if (ret != JPG_RET_SUCCESS) {
      JLOG(ERR, "JPU ENC FAILED !\n");
    } else {
      frameIdx++;
      JLOG(INFO, "JPU ENC SUCESSS !\n");
    }
  ERR_ENC:
    FreeDmabufHeapBufferAllocator(bufferAllocator);
    if (yuvFeeder != NULL) {
      YuvFeeder_Destroy(yuvFeeder);
    }
    if (writer != NULL) {
      BitstreamWriter_Destroy(writer);
    }
    if (handle != NULL) AsrJpuEncClose(handle);
    loop_count--;
  }
  if (profiling)
    JLOG(INFO, "-----frameIdx:%d ,performance:%.2f fps. ------\n", frameIdx,
         frameIdx * 1000 / total_time);

  return suc;
}

Int32 main(Int32 argc, char** argv) {
  Int32 ret = 1;
  struct option longOpt[] = {
      {"yuv", required_argument, NULL, 0},
      {"stream-endian", required_argument, NULL, 0},
      {"frame-endian", required_argument, NULL, 0},
      {"bs-size", required_argument, NULL, 0},
      {"cfg-dir", required_argument, NULL, 0},
      {"yuv-dir", required_argument, NULL, 0},
      {"output", required_argument, NULL, 0},
      {"input", required_argument, NULL, 0},
      //{ "slice-height",       required_argument,  NULL, 0 },
      //{ "enable-slice-intr",  required_argument,  NULL, 0 },
      {"quality", required_argument, NULL, 0},
      //{ "enable-tiledMode",   required_argument,  NULL, 0 },
      {"12bit", no_argument, NULL, 0},
      {"rotation", required_argument, NULL, 0},
      {"mirror", required_argument, NULL, 0},
      {"profiling", required_argument, NULL, 0},
      {"loop_count", required_argument, NULL, 0},
      {NULL, no_argument, NULL, 0},
  };

  const char* shortOpt = "fh";
  EncConfigParam config;
  Int32 c, l;
  memset((void*)&config, 0x00, sizeof(EncConfigParam));

  /* Default configurations */
  config.bsSize = STREAM_BUF_SIZE;
  strcpy(config.strCfgDir, CFG_DIR);
  strcpy(config.strYuvDir, YUV_DIR);
  strcpy(config.bitstreamFileName, DEFAULT_OUTPUT_PATH);

  while ((c = getopt_long(argc, argv, shortOpt, longOpt, &l)) != -1) {
    switch (c) {
      case 'h':
        break;
      case 0:
        if (ParseEncTestLongArgs((void*)&config, longOpt[l].name, optarg) ==
            FALSE) {
          Help(argv[0]);
        }
        break;
      default:
        Help(argv[0]);
        break;
    }
  }
  InitLog("ErrorLog.txt");
  ret = TestEncoder(&config);
  DeInitLog();

  return ret == TRUE ? 0 : 1;
}
