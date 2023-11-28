/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef _JPU_TYPES_H_
#define _JPU_TYPES_H_

#include <stdint.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t Int8;
typedef int16_t Int16;
typedef int32_t Int32;
typedef int64_t Int64;
typedef unsigned long PhysicalAddress;
typedef unsigned char BYTE;
typedef int32_t BOOL;

#ifndef NULL
#define NULL 0
#endif

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#define STATIC static

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) \
  /*lint -save -e527 -e530 */     \
  { (P) = (P); }                  \
  /*lint -restore */
#endif
typedef void* JdiDeviceCtx;

typedef enum {
  JPG_RET_SUCCESS,
  JPG_RET_FAILURE,
  JPG_RET_BIT_EMPTY,
  JPG_RET_EOS,
  JPG_RET_INVALID_HANDLE,
  JPG_RET_INVALID_PARAM,
  JPG_RET_INVALID_COMMAND,
  JPG_RET_ROTATOR_OUTPUT_NOT_SET,
  JPG_RET_ROTATOR_STRIDE_NOT_SET,
  JPG_RET_FRAME_NOT_COMPLETE,
  JPG_RET_INVALID_FRAME_BUFFER,
  JPG_RET_INSUFFICIENT_FRAME_BUFFERS,
  JPG_RET_INVALID_STRIDE,
  JPG_RET_WRONG_CALL_SEQUENCE,
  JPG_RET_CALLED_BEFORE,
  JPG_RET_NOT_INITIALIZED,
  JPG_RET_INSUFFICIENT_RESOURCE,
  JPG_RET_INST_CTRL_ERROR,
  JPG_RET_NOT_SUPPORT,
} JpgRet;

typedef enum {
  ENCODE_STATE_NEW_FRAME = 0,
  ENCODE_STATE_FRAME_DONE = 0,
  ENCODE_STATE_SLICE_DONE = 1
} EncodeState;

typedef enum {
  DECODE_STATE_NEW_FRAME = 0,
  DECODE_STATE_FRAME_DONE = 0,
  DECODE_STATE_SLICE_DONE = 1
} DecodeState;

typedef enum {
  MIRDIR_NONE,
  MIRDIR_VER,
  MIRDIR_HOR,
  MIRDIR_HOR_VER
} JpgMirrorDirection;

typedef enum {
  FORMAT_420 = 0,
  FORMAT_422 = 1,
  FORMAT_440 = 2,
  FORMAT_444 = 3,
  FORMAT_400 = 4,
  FORMAT_MAX
} FrameFormat;

typedef enum { CBCR_ORDER_NORMAL, CBCR_ORDER_REVERSED } CbCrOrder;

typedef enum {
  CBCR_SEPARATED = 0,
  CBCR_INTERLEAVE,
  CRCB_INTERLEAVE
} CbCrInterLeave;

typedef enum {
  PACKED_FORMAT_NONE,
  PACKED_FORMAT_422_YUYV,
  PACKED_FORMAT_422_UYVY,
  PACKED_FORMAT_422_YVYU,
  PACKED_FORMAT_422_VYUY,
  PACKED_FORMAT_444,
  PACKED_FORMAT_MAX
} PackedFormat;

typedef enum {
  O_FMT_NONE,
  O_FMT_420,
  O_FMT_422,
  O_FMT_444,
  O_FMT_MAX
} OutputFormat;

/* Assume that pixel endianness is big-endian.
 * b0 is low address in a framebuffer.
 * b1 is high address in a framebuffer.
 * pixel consists of b0 and b1.
 * RIGHT JUSTIFICATION: (default)
 * lsb         msb
 * |----|--------|
 * |0000| pixel  |
 * |----|--------|
 * | b0   |   b1 |
 * |-------------|
 * LEFT JUSTIFICATION:
 * lsb         msb
 * |--------|----|
 * | pixel  |0000|
 * |--------|----|
 * | b0   |   b1 |
 * |-------------|
 */
enum {
  PIXEL_16BIT_MSB_JUSTIFIED,
  PIXEL_16BIT_LSB_JUSTIFIED,
  PIXEL_16BIT_JUSTIFICATION_MAX,
};

typedef enum {
  JDI_LITTLE_ENDIAN = 0,
  JDI_BIG_ENDIAN,
  JDI_32BIT_LITTLE_ENDIAN,
  JDI_32BIT_BIG_ENDIAN,
} EndianMode;

typedef enum {
  JPU_12BIT = 0, /*12bit Uint32; 0:8bit 1:12bit default 0*/
  JPU_ROTATION,  /*rotation Uint32 0: 0, 1: 90 CCW, 2: 180 CCW, 3: 270 CCW
                    CCW(Counter Clockwise) default 0*/
  JPU_MIRROR, /*mirror Uint32 0: none, 1: vertical mirror, 2: horizontal mirror,
                 3: both default 0*/
  JPU_QUALITY,             /*image quality Uint32 [1,100] default 50*/
  JPU_HUFFMAN_TAB,         /*huffman table cfg file path char* */
  JPU_QUANT_TAB,           /*quant table cfg file path   char* */
  JPU_DISABLE_APP_MARKER,  /*disable image header app maker Uint32 0: enable
                              1:disable  defaut 0 */
  JPU_DISABLE_SOI_MARKER,  /*disable image header soi maker Uint32 0: enable
                              1:disable  defaut 0 */
  JPU_ENABLE_SOF_STUFFING, /*enable image header sof stuffing Uint32 0: enable
                              1:disable  defaut 0 */
  JPU_IMAGE_ENDIAN,        /*the endian of stream  EndianMode default
                              JDI_LITTLE_ENDIAN*/
  JPU_FRAME_BUF_ENDIAN,    /*the endian of frame  EndianMode default
                              JDI_LITTLE_ENDIAN */
} JpuParamIndex;

typedef struct {
  Uint32 picWidth;
  Uint32 picHeight;
  FrameFormat sourceFormat;
  PackedFormat packedFormat;
  CbCrInterLeave chromaInterleave;
  BOOL jpg12bit;
} EncOpenParam;

typedef struct {
  Int32 fd;
  Uint32 size;
  Uint32 viraddr;
} DmaBuffer;

typedef struct {
  DmaBuffer dmaBuffer; /* camera frame dma-buffer fd */
  Uint32 stride;       /*!<< luma stride */
  Uint32 strideC;      /*!<< chroma stride */
  Uint32 yOffset;
  Uint32 uOffset;
  Uint32 vOffset;
  FrameFormat format;
} FrameBufferInfo;

typedef struct {
  DmaBuffer dmaBuffer;
  Uint32 dataOffset;
  Uint32 imageSize;
} ImageBufferInfo;

typedef struct {
  int disableAPPMarker;
  int disableSOIMarker;
  int enableSofStuffing;
} HeaderParamSet;

typedef struct {
  CbCrInterLeave chromaInterleave;
  PackedFormat packedFormat;
  BOOL roiEnable;
  Uint32 roiOffsetX;
  Uint32 roiOffsetY;
  Uint32 roiWidth;
  Uint32 roiHeight;
  Uint32 rotation; /*!<< 0, 90, 180, 270 */
  JpgMirrorDirection mirror;
  FrameFormat outputFormat;
} DecOpenParam;

typedef struct {
  int picWidth;
  int picHeight;
  int minFrameBufferCount;
  FrameFormat sourceFormat;
  int ecsPtr;
  int roiFrameWidth;
  int roiFrameHeight;
  int roiFrameOffsetX;
  int roiFrameOffsetY;
  int roiMCUSize;
  int colorComponents;
  Uint32 bitDepth;
} JpgDecInitialInfo;
#endif /* _JPU_TYPES_H_ */
