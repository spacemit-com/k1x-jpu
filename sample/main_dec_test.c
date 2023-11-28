/*
 * Copyright (c) 2018, Chips&Media
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <getopt.h>
#include <linux/dma-buf.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "BufferAllocatorWrapper.h"
#include "jpuapi.h"
#include "jpudecapi.h"
#include "jpulog.h"
#include "main_helper.h"

#define NUM_FRAME_BUF MAX_FRAME
#define MAX_ROT_BUF_NUM 1

#ifdef SUPPORT_MULTI_INSTANCE_TEST
#else
static void Help(const char* programName) {
  JLOG(INFO,
       "-----------------------------------------------------------------------"
       "-------\n");
  JLOG(INFO, " CODAJ12 Decoder\n");
  JLOG(INFO,
       "-----------------------------------------------------------------------"
       "-------\n");
  JLOG(INFO, "%s [options] --input=jpg_file_path\n", programName);
  JLOG(INFO, "-h                      help\n");
  JLOG(INFO, "--input=FILE            jpeg filepath\n");
  JLOG(INFO, "--output=FILE           output file path\n");
  JLOG(INFO,
       "--stream-endian=ENDIAN  bitstream endianness. refer to datasheet "
       "Chapter 4.\n");
  JLOG(INFO,
       "--frame-endian=ENDIAN   pixel endianness of 16bit input source. refer "
       "to datasheet Chapter 4.\n");
  JLOG(INFO,
       "--pixelj=JUSTIFICATION  16bit-pixel justification. 0(default) - msb "
       "justified, 1 - lsb justified in little-endianness\n");
  JLOG(INFO, "--bs-size=SIZE          bitstream buffer size in byte\n");
  JLOG(INFO, "--roi=x,y,w,h           ROI region\n");
  JLOG(INFO,
       "--subsample             conversion sub-sample(ignore case): NONE, 420, "
       "422, 444\n");
  JLOG(INFO,
       "--ordering              conversion ordering(ingore-case): NONE, NV12, "
       "NV21, YUYV, YVYU, UYVY, VYUY, AYUV\n");
  JLOG(INFO, "                        NONE - planar format\n");
  JLOG(INFO,
       "                        NV12, NV21 - semi-planar format for all the "
       "subsamples.\n");
  JLOG(INFO,
       "                                     If subsample isn't defined or is "
       "none, the sub-sample depends on jpeg information\n");
  JLOG(INFO,
       "                                     The subsample 440 can be "
       "converted to the semi-planar format. It means that the encoded "
       "sub-sample should be 440.\n");
  JLOG(INFO,
       "                        YUVV..VYUY - packed format. subsample be "
       "ignored.\n");
  JLOG(INFO,
       "                        AYUV       - packed format. subsample be "
       "ignored.\n");
  JLOG(INFO, "--rotation              0, 90, 180, 270\n");
  JLOG(INFO, "--mirror                0(none), 1(V), 2(H), 3(VH)\n");
  JLOG(INFO,
       "--scaleH                Horizontal downscale: 0(none), 1(1/2), 2(1/4), "
       "3(1/8)\n");
  JLOG(INFO,
       "--scaleV                Vertical downscale  : 0(none), 1(1/2), 2(1/4), "
       "3(1/8)\n");
  JLOG(INFO,
       "--profiling             0: performance output will not be printed "
       "1:print performance output \n");
  JLOG(INFO, "--loop_count            loop count\n");
  exit(1);
}
#endif /* SUPPORT_MULTI_INSTANCE_TEST */

BOOL TestDecoder(DecConfigParam* param) {
  // JpgDecHandle        handle        = {0};
  DecOpenParam openParam;
  void* handle = NULL;
  ImageBufferInfo jpegImageBuffer;
  FrameBufferInfo* frameBuffer;
  JpgDecInitialInfo initialInfo;
  JpgRet ret = JPG_RET_SUCCESS;
  Uint32 framebufWidth = 0, framebufHeight = 0, framebufStride = 0;
  Int32 i = 0, frameIdx = 0, saveIdx = 0, totalNumofErrMbs = 0;
  BOOL suc = FALSE;
  Uint8* pYuv = NULL;
  FILE* fpYuv = NULL;
  Int32 int_reason = 0;
  Int32 instIdx = 0;
  Uint32 outbufSize = 0;
  DecConfigParam decConfig;
  Uint32 decodingWidth, decodingHeight;
  FrameFormat subsample;
  Uint32 bitDepth = 8;
  Uint32 temp;
  BOOL scalerOn = FALSE;
  BSFeeder feeder;
  BufferAllocator* bufferAllocator = NULL;
  Uint32 imagesize = 0;
  Uint32 imageBufSize = 0;
  Uint32 profiling = 0;
  Uint32 loop_count = 1;
  struct timeval start_time;
  struct timeval end_time;
  double total_time = 0;

  memcpy(&decConfig, param, sizeof(DecConfigParam));
  openParam.chromaInterleave = decConfig.cbcrInterleave;
  openParam.packedFormat = decConfig.packedFormat;
  openParam.outputFormat = decConfig.subsample;
  profiling = decConfig.profiling;
  loop_count = decConfig.loop_count;
  if (loop_count) {
    bufferAllocator = CreateDmabufHeapBufferAllocator();
    ret = AsrJpuDecOpen(&handle, &openParam);
    if (ret != JPG_RET_SUCCESS && ret != JPG_RET_CALLED_BEFORE) {
      suc = 0;
      JLOG(ERR, "AsrJpuDecOpen failed Error code is 0x%x \n", ret);
      goto ERR_DEC;
    }

    if ((feeder = BitstreamFeeder_Create(
             decConfig.bitstreamFileName, decConfig.feedingMode,
             (EndianMode)decConfig.StreamEndian)) == NULL) {
      goto ERR_DEC;
    }

    if (strlen(decConfig.yuvFileName)) {
      if ((fpYuv = fopen(decConfig.yuvFileName, "wb")) == NULL) {
        JLOG(ERR, "Can't open %s \n", decConfig.yuvFileName);
        goto ERR_DEC;
      }
    }

    imageBufSize = (decConfig.bsSize == 0) ? STREAM_BUF_SIZE : decConfig.bsSize;
    imageBufSize = (imageBufSize + 1023) & ~1023;
    jpegImageBuffer.dmaBuffer.size = imageBufSize;
    jpegImageBuffer.dataOffset = 0;
    JLOG(INFO, "intput imagesize :%d", imageBufSize);
    jpegImageBuffer.dmaBuffer.fd = DmabufHeapAllocSystem(
        bufferAllocator, true, jpegImageBuffer.dmaBuffer.size, 0, 0);

    if (jpegImageBuffer.dmaBuffer.fd < 0) {
      JLOG(ERR,
           "#### DmabufHeapAllocSystem alloc stream fd:%d size:%d is invaild "
           "####\n",
           jpegImageBuffer.dmaBuffer.fd, jpegImageBuffer.dmaBuffer.size);
      goto ERR_DEC;
    }
    /* Fill jpeg data in the bitstream buffer */
    imagesize = BitstreamFeeder_Act(feeder, handle, &jpegImageBuffer);
    jpegImageBuffer.imageSize = imagesize;
    if (profiling) gettimeofday(&start_time, 0);
    ret = AsrJpuDecGetInitialInfo(handle, &jpegImageBuffer, &initialInfo);
    bitDepth = initialInfo.bitDepth;
    if (initialInfo.sourceFormat == FORMAT_420 ||
        initialInfo.sourceFormat == FORMAT_422)
      framebufWidth = JPU_CEIL(16, initialInfo.picWidth);
    else
      framebufWidth = JPU_CEIL(8, initialInfo.picWidth);

    if (initialInfo.sourceFormat == FORMAT_420 ||
        initialInfo.sourceFormat == FORMAT_440)
      framebufHeight = JPU_CEIL(16, initialInfo.picHeight);
    else
      framebufHeight = JPU_CEIL(8, initialInfo.picHeight);

    decodingWidth = framebufWidth >> decConfig.iHorScaleMode;
    decodingHeight = framebufHeight >> decConfig.iVerScaleMode;
    if (decConfig.packedFormat != PACKED_FORMAT_NONE &&
        decConfig.packedFormat != PACKED_FORMAT_444) {
      // When packed format, scale-down resolution should be multiple of 2.
      decodingWidth = JPU_CEIL(2, decodingWidth);
    }

    subsample = initialInfo.sourceFormat;
    temp = decodingWidth;
    decodingWidth = (decConfig.rotation == 90 || decConfig.rotation == 270)
                        ? decodingHeight
                        : decodingWidth;
    decodingHeight = (decConfig.rotation == 90 || decConfig.rotation == 270)
                         ? temp
                         : decodingHeight;
    if (decConfig.roiEnable == TRUE) {
      decodingWidth = framebufWidth = initialInfo.roiFrameWidth;
      decodingHeight = framebufHeight = initialInfo.roiFrameHeight;
    }
    outbufSize = decodingWidth * decodingHeight * 3 * (bitDepth + 7) / 8;
    if ((pYuv = malloc(outbufSize)) == NULL) {
      JLOG(ERR, "Fail to allocation memory for display buffer\n");
      goto ERR_DEC;
    }
    JLOG(INFO, "<INSTANCE %d>\n", instIdx);
    JLOG(INFO, "SOURCE PICTURE SIZE : W(%d) H(%d)\n", initialInfo.picWidth,
         initialInfo.picHeight);
    JLOG(INFO, "DECODED PICTURE SIZE: W(%d) H(%d)\n", decodingWidth,
         decodingHeight);
    JLOG(INFO, "SUBSAMPLE           : %d\n", subsample);

    frameBuffer = AllocateFrameBuffer(
        bufferAllocator, instIdx, subsample, decConfig.cbcrInterleave,
        decConfig.packedFormat, decConfig.rotation, scalerOn, decodingWidth,
        decodingHeight, bitDepth);

    if (frameBuffer == NULL) {
      JLOG(ERR, "Failed to AllocateFrameBuffer()\n");
      goto ERR_DEC;
    }

    ret = AsrJpuDecStartOneFrame(handle, frameBuffer, &jpegImageBuffer);
    if (profiling) {
      gettimeofday(&end_time, 0);
      total_time += (end_time.tv_sec - start_time.tv_sec) * 1000.f +
                    (end_time.tv_usec - start_time.tv_usec) / 1000.f;
    }
    frameIdx++;
    if (!SaveYuvImageHelperFormat_V20(
            bufferAllocator, fpYuv, pYuv, frameBuffer, decConfig.cbcrInterleave,
            decConfig.packedFormat, decodingWidth, decodingHeight, bitDepth)) {
      goto ERR_DEC;
    }

  ERR_DEC:
    // Now that we are done with decoding, close the open instance.
    FreeDmabufHeapBufferAllocator(bufferAllocator);
    ret = AsrJpuDecClose(handle);
    if (ret != JPG_RET_SUCCESS) suc = 0;
    BitstreamFeeder_Destroy(feeder);
    FreeFrameBuffer(frameBuffer);
    if (jpegImageBuffer.dmaBuffer.fd >= 0) close(jpegImageBuffer.dmaBuffer.fd);
    loop_count--;
  }
  if (profiling)
    JLOG(INFO, "-----frameIdx:%d ,performance:%.2f fps. ------\n", frameIdx,
         frameIdx * 1000 / total_time);
  JLOG(INFO, "jpu dec done.");
  return 0;
}

Int32 main(Int32 argc, char** argv) {
  Int32 ret = 1;
  struct option longOpt[] = {
      {"output", required_argument, NULL, 0},
      {"input", required_argument, NULL, 0},
      {"pixelj", required_argument, NULL, 0},
      {"bs-size", required_argument, NULL, 0},
      {"roi", required_argument, NULL, 0},
      {"subsample", required_argument, NULL, 0},
      {"ordering", required_argument, NULL, 0},
      {"rotation", required_argument, NULL, 0},
      {"mirror", required_argument, NULL, 0},
      {"scaleH", required_argument, NULL, 0},
      {"scaleV", required_argument, NULL, 0},
      {"profiling", required_argument, NULL, 0},
      {"loop_count", required_argument, NULL, 0},
      {NULL, no_argument, NULL, 0},
  };
  Int32 c, l;
  const char* shortOpt = "fh";
  DecConfigParam config;

  memset((void*)&config, 0x00, sizeof(DecConfigParam));
  config.subsample = FORMAT_MAX;

  while ((c = getopt_long(argc, argv, shortOpt, longOpt, &l)) != -1) {
    switch (c) {
      case 'h':
        Help(argv[0]);
        break;
      case 0:
        if (ParseDecTestLongArgs((void*)&config, longOpt[l].name, optarg) ==
            FALSE) {
          Help(argv[0]);
        }
        break;
      default:
        Help(argv[0]);
        break;
    }
  }

  /* CHECK PARAMETERS */
  if ((config.iHorScaleMode || config.iVerScaleMode) && config.roiEnable) {
    JLOG(ERR, "Invalid operation mode : ROI cannot work with the scaler\n");
    return 1;
  }
  if (config.packedFormat && config.roiEnable) {
    JLOG(ERR,
         "Invalid operation mode : ROI cannot work with the packed format "
         "conversion\n");
    return 1;
  }
  if (config.roiEnable && (config.rotation || config.mirror)) {
    JLOG(ERR, "Invalid operation mode : ROI cannot work with the PPU.\n");
  }
  ret = TestDecoder(&config);
  return ret == TRUE ? 0 : 1;
}
