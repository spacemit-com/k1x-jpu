
#ifndef JPUHELPER_H_INCLUDED
#define JPUHELPER_H_INCLUDED
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpuapi.h"
#include "jpulog.h"
#include "yuv_feeder.h"

#define MAX_FILE_PATH 256

typedef enum {
  FEEDING_METHOD_FIXED_SIZE,
  FEEDING_METHOD_FRAME_SIZE, /*!<< use FFMPEG demuxer */
  FEEDING_METHOD_MAX
} FeedingMethod;

typedef enum { BSWRITER_ES, BSWRITER_CONTAINER, BSWRITER_MAX } BSWriterType;

typedef struct {
  char yuvFileName[MAX_FILE_PATH];
  char bitstreamFileName[MAX_FILE_PATH];
  char huffFileName[MAX_FILE_PATH];
  char qMatFileName[MAX_FILE_PATH];
  char cfgFileName[MAX_FILE_PATH];
  Uint32 picWidth;
  Uint32 picHeight;
  Uint32 mjpgChromaFormat;
  Uint32 mjpgFramerate;
  Uint32 outNum;

  Uint32 StreamEndian;
  Uint32 FrameEndian;
  FrameFormat sourceSubsample;
  CbCrInterLeave chromaInterleave;
  PackedFormat packedFormat;
  Uint32 bEnStuffByte;
  Uint32 encHeaderMode;

  char strCfgDir[MAX_FILE_PATH];
  char strYuvDir[MAX_FILE_PATH];

  Uint32 bsSize;
  Uint32 encQualityPercentage;
  Uint32 tiledModeEnable;
  Uint32 sliceHeight;
  Uint32 sliceInterruptEnable;
  BOOL extendedSequential;
  Uint32 rotation;
  JpgMirrorDirection mirror;
  BSWriterType writerType;
  Uint32 profiling;
  Uint32 loop_count;
} EncConfigParam;

typedef struct {
  char yuvFileName[MAX_FILE_PATH];
  char bitstreamFileName[MAX_FILE_PATH];
  Uint32 outNum;
  Uint32 checkeos;
  Uint32 StreamEndian;
  Uint32 FrameEndian;
  Uint32 iHorScaleMode;
  Uint32 iVerScaleMode;
  // ROI
  Uint32 roiEnable;
  Uint32 roiWidth;
  Uint32 roiHeight;
  Uint32 roiOffsetX;
  Uint32 roiOffsetY;
  Uint32 roiWidthInMcu;
  Uint32 roiHeightInMcu;
  Uint32 roiOffsetXInMcu;
  Uint32 roiOffsetYInMcu;
  Uint32 rotation;
  JpgMirrorDirection mirror;
  FrameFormat subsample;
  PackedFormat packedFormat;
  CbCrInterLeave cbcrInterleave;
  Uint32 bsSize;
  Uint32 sliceHeight;
  BOOL sliceInterruptEnable;
  FeedingMethod feedingMode;
  Uint32 profiling;
  Uint32 loop_count;
} DecConfigParam;

typedef struct {
  char SrcFileName[256];
  Uint32 NumFrame;
  Uint32 PicX;
  Uint32 PicY;
  Uint32 FrameRate;

  // MJPEG ONLY
  char HuffTabName[256];
  char QMatTabName[256];
  Uint32 VersionID;
  Uint32 FrmFormat;
  FrameFormat SrcFormat;
  Uint32 RstIntval;
  Uint32 ThumbEnable;
  Uint32 ThumbSizeX;
  Uint32 ThumbSizeY;
  Uint32 prec;
  Uint32 QMatPrec0;
  Uint32 QMatPrec1;
} ENC_CFG;

typedef enum {
  JPU_ENCODER,
  JPU_DECODER,
  JPU_NONE,
} JPUComponentType;

typedef enum {
  YUV444,
  YUV422,
  YUV420,
  NV12,
  NV21,
  YUV400,
  YUYV,
  YVYU,
  UYVY,
  VYUY,
  YYY,
  RGB_PLANAR,
  RGB32,
  RGB24,
  RGB16
} yuv2rgb_color_format;

#if defined(__cplusplus)
extern "C" {
#endif

extern BOOL TestDecoder(DecConfigParam* param);
extern BOOL TestEncoder(EncConfigParam* param);
extern int jpgGetHuffTable(char* huffFileName, EncMjpgParam* param, int prec);
extern int jpgGetQMatrix(char* qMatFileName, EncMjpgParam* param);
extern BOOL GetJpgEncOpenParam(EncOpenParam* pEncOP,
                               EncConfigParam* pEncConfig);
extern int parseJpgCfgFile(ENC_CFG* pEncCfg, char* FileName);

extern JpgRet ReadJpgBsBufHelper(JpgEncHandle handle, FILE* bsFp,
                                 JpgEncOpenParam* pEncOP,
                                 JpgEncOutputInfo* pEncOutput);

extern int LoadYuvImageHelperFormat_V20(int prec, FILE* yuvFp, Uint8* pYuv,
                                        PhysicalAddress addrY,
                                        PhysicalAddress addrCb,
                                        PhysicalAddress addrCr, int picWidth,
                                        int picHeight, int stride,
                                        int interleave, int format, int endian,
                                        int packed, Uint32 justification);

extern int SaveYuvImageHelperFormat_V20(BufferAllocator* bufferAllocator,
                                        FILE* yuvFp, Uint8* pYuv,
                                        FrameBufferInfo* fb,
                                        CbCrInterLeave interLeave,
                                        PackedFormat packed, Uint32 picWidth,
                                        Uint32 picHeight, Uint32 bitDepth);

extern int GetFrameBufSize(int framebufFormat, int picWidth, int picHeight);
extern void GetMcuUnitSize(int format, int* mcuWidth, int* mcuHeight);
extern FrameBufferInfo* AllocateFrameBuffer(
    BufferAllocator* bufferAllocator, Uint32 instIdx, FrameFormat subsample,
    CbCrInterLeave cbcrIntlv, PackedFormat packed, Uint32 rotation,
    BOOL scalerOn, Uint32 width, Uint32 height, Uint32 bitDepth);
void FreeFrameBuffer(FrameBufferInfo* frameBufferInfo);
// DPBBufSize may not same with FrameBufSize due to format convert rounding
extern int GetDPBBufSize(int framebufFormat, int picWidth, int picHeight,
                         int picWidth_C, int interleave);
extern BOOL ParseDecTestLongArgs(void* config, const char* argName,
                                 char* value);
extern BOOL ParseEncTestLongArgs(void* config, const char* argName,
                                 char* value);

/* --------------------------------------------------------------------------
 * BS feeder
   -------------------------------------------------------------------------- */
typedef struct {
  void* data;
  Uint32 size;
  BOOL eos;  //!<< End of stream
} BSChunk;

typedef void* BSFeeder;

extern BSFeeder BitstreamFeeder_Create(const char* path, FeedingMethod method,
                                       EndianMode endian);
extern Uint32 BitstreamFeeder_Act(BSFeeder feeder, JpgDecHandle handle,
                                  ImageBufferInfo* bsBuffer);
extern BOOL BitstreamFeeder_Destroy(BSFeeder feeder);
extern BOOL BitstreamFeeder_IsEos(BSFeeder feeder);

/* --------------------------------------------------------------------------
 * BS writer
   -------------------------------------------------------------------------- */
typedef void* BSWriter;

typedef struct BitstreamWriterImpl {
  void* context;
  BOOL (*Create)
  (struct BitstreamWriterImpl* impl, EncConfigParam* config, const char* path);
  Uint32 (*Act)(struct BitstreamWriterImpl* impl, Uint8* es, Uint32 size);
  BOOL (*Destroy)(struct BitstreamWriterImpl* impl);
} BitstreamWriterImpl;

extern BSWriter BitstreamWriter_Create(BSWriterType type,
                                       EncConfigParam* config,
                                       const char* path);
extern BOOL BitstreamWriter_Act(BSWriter writer, Uint8* es, Uint32 size,
                                BOOL delayedWrite);
extern void BitstreamWriter_Destroy(BSWriter writer);

typedef struct {
  FeedingMethod method;
  Uint8* remainData;
  Uint32 remainDataSize;
  Uint32 remainOffset;
  void* actualFeeder;
  Uint32 room;
  BOOL eos;
  EndianMode endian;
} BitstreamFeeder;

void* BSFeederFixedSize_Create(const char* path);
BOOL BSFeederFixedSize_Destroy(void* feeder);
Int32 BSFeederFixedSize_Act(void* feeder, ImageBufferInfo* bsBuffer);
void BSFeederFixedSize_SetFeedingSize(void* feeder, Uint32 feedingSize);
/* --------------------------------------------------------------------------
 * String
   -------------------------------------------------------------------------- */
extern char* GetFileExtension(const char* filename);

#if defined(__cplusplus)
}
#endif

#endif /* JPUHELPER_H_INCLUDED */
