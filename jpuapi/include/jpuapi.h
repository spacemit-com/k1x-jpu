/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef JPUAPI_H_INCLUDED
#define JPUAPI_H_INCLUDED

#include "jpuconfig.h"
#include "jputypes.h"

/* _n: number, _s: significance */
#define JPU_CEIL(_s, _n) (((_n) + (_s - 1)) & ~(_s - 1))
#define JPU_FLOOR(_s, _n) (_n & ~(_s - 1))
//------------------------------------------------------------------------------
// common struct and definition
//------------------------------------------------------------------------------
#define API_VERSION 0x124
#define DC_TABLE_INDEX0 0
#define AC_TABLE_INDEX0 1
#define DC_TABLE_INDEX1 2
#define AC_TABLE_INDEX1 3

#define DC_TABLE_INDEX2 4
#define AC_TABLE_INDEX2 5
#define DC_TABLE_INDEX3 6
#define AC_TABLE_INDEX3 7

#define Q_COMPONENT0 0
#define Q_COMPONENT1 0x40
#define Q_COMPONENT2 0x80
#define THTC_LIST_CNT 8

typedef struct {
  Uint32 sourceFormat;
  Uint32 restartInterval;
  BYTE huffBits[8][256];
  BYTE huffVal[8][256];
  short qMatTab[4][64];
  BOOL lumaQ12bit;
  BOOL chromaQ12bit;
  BOOL extendedSequence; /* 12bit JPEG */
} EncMjpgParam;

typedef enum {
  /* Non-differential, Huffman coding */
  JPEG_BASELINE_DCT,
  JPEG_EXTENDED_SEQUENTIAL_DCT,
  /* The others are not supported on CODAJ12 */
} JpgProfile;

typedef enum {
  SET_JPG_SCALE_HOR,
  SET_JPG_SCALE_VER,
  SET_JPG_USE_STUFFING_BYTE_FF,
  ENC_JPG_GET_HEADER,
  ENABLE_LOGGING,
  DISABLE_LOGGING,
  JPG_CMD_END
} JpgCommand;

typedef enum {
  INT_JPU_DONE = 0,
  INT_JPU_ERROR = 1,
  INT_JPU_BIT_BUF_EMPTY = 2,
  INT_JPU_BIT_BUF_FULL = 2,
  INT_JPU_OVERFLOW,
} InterruptJpu;

typedef enum { JPG_TBL_NORMAL, JPG_TBL_MERGE } JpgTableMode;

typedef enum {
  ENC_HEADER_MODE_NORMAL,
  ENC_HEADER_MODE_SOS_ONLY
} JpgEncHeaderMode;

enum {
  JPG_SCALE_DOWN_NONE,
  JPG_SCALE_DOWN_ONE_HALF,
  JPG_SCALE_DOWN_ONE_QUARTER,
  JPG_SCALE_DOWN_ONE_EIGHTS,
  JPG_SCALE_DOWN_MAX
};

enum {
  PRODUCT_ID_CODAJ10 = 1,
};

struct JpgInst;
typedef struct JpgInst *JpgHandle;

//------------------------------------------------------------------------------
// decode struct and definition
//------------------------------------------------------------------------------

typedef struct JpgInst JpgDecInst;
typedef JpgDecInst *JpgDecHandle;

/* JPU Capabilities */
typedef struct {
  Uint32 productId; /*!<< Product ID number */
  Uint32 revisoin;  /*!<< Revision number */
  BOOL
      support12bit; /*!<< able to encode or decode a extended sequential JPEG */
} JPUCap;

typedef struct {
  Uint32 bitstreamBufferFd;
  Uint32 bitstreamBufferSize;
  BYTE *pBitStream;
  Uint32 streamEndian;
  Uint32 frameEndian;
  CbCrInterLeave chromaInterleave;
  BOOL thumbNailEn;
  PackedFormat packedFormat;
  BOOL roiEnable;
  Uint32 roiOffsetX;
  Uint32 roiOffsetY;
  Uint32 roiWidth;
  Uint32 roiHeight;
  Uint32 sliceHeight;
  Uint32 intrEnableBit;
  Uint32 rotation;           /*!<< 0, 90, 180, 270 */
  JpgMirrorDirection mirror; /*!<< 0(none), 1(vertical), 2(mirror), 3(both) */
  FrameFormat outputFormat;
  BOOL sliceInstMode;
} JpgDecOpenParam;

typedef struct {
  int scaleDownRatioWidth;
  int scaleDownRatioHeight;
} JpgDecParam;

typedef struct {
  int indexFrameDisplay;
  int numOfErrMBs;
  int decodingSuccess;
  int decPicHeight;
  int decPicWidth;
  int consumedByte;
  int bytePosFrameStart;
  int ecsPtr;
  Uint32 frameCycle; /*!<< clock cycle */
  Uint32 rdPtr;
  Uint32 wrPtr;
  Uint32 decodedSliceYPos;
  DecodeState decodeState;
  int intStatus;
} JpgDecOutputInfo;

typedef struct {
  BYTE *buffer;
  int index;
  int size;
} vpu_getbit_context_t;

typedef struct {
  PhysicalAddress streamWrPtr;
  PhysicalAddress streamRdPtr;
  int streamEndflag;
  PhysicalAddress streamBufStartAddr;
  PhysicalAddress streamBufEndAddr;
  int streamBufSize;
  Uint32 streamFd;
  BYTE *pBitStream;

  int frameOffset;
  int consumeByte;
  int nextOffset;
  int currOffset;

  FrameBufferInfo *frameBufPool;
  int numFrameBuffers;
  int stride;
  int initialInfoObtained;
  int minFrameBufferNum;
  int streamEndian;
  int frameEndian;
  Uint32 chromaInterleave;

  Uint32 picWidth;
  Uint32 picHeight;
  Uint32 alignedWidth;
  Uint32 alignedHeight;
  int headerSize;
  int ecsPtr;
  int pagePtr;
  int wordPtr;
  int bitPtr;
  FrameFormat format;
  int rstIntval;

  int userHuffTab;
  int userqMatTab;

  int huffDcIdx;
  int huffAcIdx;
  int Qidx;

  BYTE huffVal[8][256];
  BYTE huffBits[8][256];
  short qMatTab[4][64];
  Uint32 huffMin[8][16];
  Uint32 huffMax[8][16];
  BYTE huffPtr[8][16];
  BYTE cInfoTab[4][6];

  int busReqNum;
  int compNum;
  int mcuBlockNum;
  int compInfo[3];
  int frameIdx;
  int bitEmpty;
  int iHorScaleMode;
  int iVerScaleMode;
  Uint32 mcuWidth;
  Uint32 mcuHeight;
  vpu_getbit_context_t gbc;

  // ROI
  Uint32 roiEnable;
  Uint32 roiOffsetX;
  Uint32 roiOffsetY;
  Uint32 roiWidth;
  Uint32 roiHeight;
  Uint32 roiMcuOffsetX;
  Uint32 roiMcuOffsetY;
  Uint32 roiMcuWidth;
  Uint32 roiMcuHeight;
  Uint32 roiFrameWidth;
  Uint32 roiFrameHeight;
  PackedFormat packedFormat;

  int jpg12bit;
  int q_prec0;
  int q_prec1;
  int q_prec2;
  int q_prec3;
  Uint32 ofmt;
  Uint32 stride_c;
  Uint32 bitDepth;
  Uint32 sliceHeight;
  Uint32 intrEnableBit;
  Uint32 decIdx;
  Uint32 decSlicePosY;
  BOOL firstSliceDone;
  Uint32 rotationIndex;           /*!<< 0: 0, 1: 90 CCW, 2: 180 CCW, 3: 270 CCW
                                     CCW(Counter Clockwise)*/
  JpgMirrorDirection mirrorIndex; /*!<< 0: none, 1: vertical mirror, 2:
                                     horizontal mirror, 3: both */
  Int32 thtc[THTC_LIST_CNT]; /*!<< Huffman table definition length and table
                                class list : -1 indicates not exist. */
  Uint32 numHuffmanTable;
} JpgDecInfo;

typedef struct {
  PhysicalAddress streamRdPtr;
  PhysicalAddress streamWrPtr;
  PhysicalAddress streamBufStartAddr;
  PhysicalAddress streamBufEndAddr;

  FrameBufferInfo *frameBufPool;
  int numFrameBuffers;
  int stride;
  int initialInfoObtained;

  Uint32 streamFd;
  Uint32 streamSize;
  Uint32 streamBodyOffset;
  Uint32 srcWidth;
  Uint32 srcHeight;
  Uint32 picWidth;
  Uint32 picHeight;
  Uint32 alignedWidth;
  Uint32 alignedHeight;
  Uint32 mcuWidth;
  Uint32 mcuHeight;
  int seqInited;
  int frameIdx;
  FrameFormat format;

  int streamEndian;
  int frameEndian;
  Uint32 chromaInterleave;

  int rstIntval;
  int busReqNum;
  int mcuBlockNum;
  int compNum;
  int compInfo[3];

  // give command
  int disableAPPMarker;
  int quantMode;
  int disableSOIMarker;
  int stuffByteEnable;

  Uint32 huffCode[8][256];
  Uint32 huffSize[8][256];
  BYTE pHuffVal[8][256];
  BYTE pHuffBits[8][256];
  // short*  pQMatTab[4];
  short pQMatTab[4][64];
  int jpg12bit;
  int q_prec0;
  int q_prec1;

  BYTE *pCInfoTab[4];

  PackedFormat packedFormat;

  Int32 sliceHeight;
  Uint32 intrEnableBit;
  Uint32 encIdx;
  Uint32 encSlicePosY;
  Uint32 tiledModeEnable;
  Uint32 rotationIndex; /*!<< 0: 0, 1: 90 CCW, 2: 180 CCW, 3: 270 CCW
                           CCW(Counter Clockwise)*/
  Uint32 mirrorIndex;   /*!<< 0: none, 1: vertical mirror, 2: horizontal mirror,
                           3: both */
} JpgEncInfo;

typedef struct JpgInst {
  Int32 inUse;
  Int32 instIndex;
  Int32 loggingEnable;
  BOOL sliceInstMode;
  JdiDeviceCtx devctx;
  union {
    JpgEncInfo encInfo;
    JpgDecInfo decInfo;
  } * JpgInfo;
} JpgInst;

//------------------------------------------------------------------------------
// encode struct and definition
//------------------------------------------------------------------------------

typedef struct JpgInst JpgEncInst;
typedef JpgEncInst *JpgEncHandle;

typedef struct {
  Uint32 bitstreamBufferFd;
  Uint32 bitstreamBufferSize;
  Uint32 bitstreamBodyOffset;
  Uint32 srcWidth;
  Uint32 srcHeight;
  Uint32 picWidth;
  Uint32 picHeight;
  FrameFormat sourceFormat;
  Uint32 restartInterval;
  Uint32 streamEndian;
  Uint32 frameEndian;
  CbCrInterLeave chromaInterleave;
  BYTE huffVal[8][256];
  BYTE huffBits[8][256];
  short qMatTab[4][64];
  BOOL jpg12bit;
  BOOL q_prec0;
  BOOL q_prec1;
  PackedFormat packedFormat;
  Uint32 tiledModeEnable;
  Uint32 sliceHeight;
  Uint32 intrEnableBit;
  BOOL sliceInstMode;
  Uint32 rotation;
  Uint32 mirror;
} JpgEncOpenParam;

typedef struct {
  FrameBufferInfo *sourceFrame;
} JpgEncParam;

typedef struct {
  Uint32 bitstreamBufferFd;
  Uint32 bitstreamSize;
  PhysicalAddress streamRdPtr;
  PhysicalAddress streamWrPtr;
  Uint32 encodedSliceYPos;
  EncodeState encodeState;
  Uint32 intStatus;
  Uint32 frameCycle; /*!<< clock cycle */
} JpgEncOutputInfo;

typedef struct {
  PhysicalAddress paraSet;
  BYTE *pParaSet;
  int size;
  int headerMode;
  int quantMode;
  int huffMode;
  int disableAPPMarker;
  int disableSOIMarker;
  int enableSofStuffing;
} JpgEncParamSet;

//------------------------------------------------------------------------------
// framebuffer struct and definition
//------------------------------------------------------------------------------
struct codaJ12V_framebuffer_attribute {
  /** picture size **/
  int width;
  int height;

  /** aligned size jpu internal needed **/
  int width_align;
  int height_align;

  /** luma **/
  int luma_stride;
  int luma_height;
  int luma_size;

  /** chroma **/
  int chroma_stride;
  int chroma_height;
  int chroma_size;

  uint32_t fb_size;
  uint32_t cb_offset;
  uint32_t cr_offset;
};

struct codaJ12V_vcodec_parameters {
  /** raw YUV frame pixel format **/
  int pix_fmt;

  /** JPEG image picture size **/
  int width;
  int height;

  /** rotation: 0, 90, 270 **/
  int rotation;

  /** 0 - off, 1 - on **/
  int scale_on;

  /** JPEG image sample factor: 0-420, 1-422 2-440 3-444, 4-400 **/
  int sample_factor;

  /** pixel sample bits: 8, 12 **/
  int sample_bits;
  /** 1:decoder 0:encoder  when it is the decoder we so set luma stride = chroma
   * stride YUV420**/
  int is_dec;
  /** align_sie for yuv stride **/
  uint32_t align_size;
};

#ifdef __cplusplus
extern "C" {
#endif

int jdi_write_memory(JdiDeviceCtx devctx, unsigned char *addr,
                     unsigned char *data, int len, int endian);
int jdi_read_memory(JdiDeviceCtx devctx, unsigned char *addr,
                    unsigned char *data, int len, int endian);
Uint32 JPU_IsInit(JdiDeviceCtx devctx);

int JPU_IsBusy(JpgHandle handle);
Uint32 JPU_GetStatus(JpgHandle handle);
void JPU_ClrStatus(JpgHandle handle, Uint32 val);

JpgRet JPU_Init(int dev_id, JdiDeviceCtx *ctx);
void JPU_DeInit(JdiDeviceCtx devctx);
int JPU_GetOpenInstanceNum(JdiDeviceCtx devctx);
JpgRet JPU_GetVersionInfo(JdiDeviceCtx devctx, Uint32 *apiVersion,
                          Uint32 *hwRevision, Uint32 *hwProductId);

// function for decode
JpgRet JPU_DecOpen(JdiDeviceCtx devctx, JpgDecHandle *, JpgDecOpenParam *);
JpgRet JPU_DecClose(JpgDecHandle);
JpgRet JPU_DecGetInitialInfo(JpgDecHandle handle, JpgDecInitialInfo *info);
JpgRet JPU_DecRegisterFrameBuffer(JpgDecHandle handle,
                                  FrameBufferInfo *bufArray, int num,
                                  int stride);
JpgRet JPU_DecGetBitstreamBuffer(JpgDecHandle handle, PhysicalAddress *prdPrt,
                                 PhysicalAddress *pwrPtr, int *size);
JpgRet JPU_DecUpdateBitstreamBuffer(JpgDecHandle handle, int size);
JpgRet JPU_HWReset(JdiDeviceCtx devctx);
JpgRet JPU_SWReset(JpgHandle handle, JdiDeviceCtx devctx);
JpgRet JPU_DecStartOneFrame(JpgDecHandle handle, JpgDecParam *param);
JpgRet JPU_DecGetOutputInfo(JpgDecHandle handle, JpgDecOutputInfo *info);
JpgRet JPU_DecGiveCommand(JpgDecHandle handle, JpgCommand cmd, void *parameter);
JpgRet JPU_DecSetRdPtr(JpgDecHandle handle, PhysicalAddress addr,
                       BOOL updateWrPtr);
JpgRet JPU_DecSetRdPtrEx(JpgDecHandle handle, PhysicalAddress addr,
                         BOOL updateWrPtr);

// function for encode
JpgRet JPU_EncOpenParamDefault(JpgEncOpenParam *pEncOP);
JpgRet JPU_EncHandleRotaion(JpgEncInfo *pEncInfo, Uint32 rotationIndex);
JpgRet JPU_EncOpen(JdiDeviceCtx devctx, JpgEncHandle *pHandle,
                   JpgEncOpenParam *pParam);
JpgRet JPU_EncClose(void *pHandle);
JpgRet JPU_EncStartOneFrame(JpgEncHandle handle, JpgEncParam *param);
Int32 JPU_WaitInterrupt(JpgHandle handle, int timeout);
JpgRet JPU_EncGetOutputInfo(void *handle, JpgEncOutputInfo *info);
JpgRet JPU_EncGiveCommand(JpgEncHandle handle, JpgCommand cmd, void *parameter);

int JPU_ShowRegisters(JpgHandle handle);

#ifdef __cplusplus
}
#endif

#endif
