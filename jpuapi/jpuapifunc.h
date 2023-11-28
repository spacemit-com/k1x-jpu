/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef JPUAPI_UTIL_H_INCLUDED
#define JPUAPI_UTIL_H_INCLUDED

#include "jdi.h"
#include "jpuapi.h"
#include "jputypes.h"

#define JpuWriteInstReg(devctx, INST_IDX, ADDR, DATA)                         \
  jdi_write_register(devctx, ((unsigned long)INST_IDX * NPT_REG_SIZE) + ADDR, \
                     DATA)  // system register write 	with instance index
#define JpuReadInstReg(devctx, INST_IDX, ADDR)           \
  jdi_read_register(                                     \
      devctx, ((unsigned long)INST_IDX * NPT_REG_SIZE) + \
                  ADDR)  // system register write 	with instance index
#define JpuWriteReg(devctx, ADDR, DATA) \
  jdi_write_register(devctx, ADDR, DATA)  // system register write
#define JpuReadReg(devctx, ADDR) \
  jdi_read_register(devctx, ADDR)  // system register write
#define JpuWriteMem(devctx, ADDR, DATA, LEN, ENDIAN) \
  jdi_write_memory(devctx, ADDR, DATA, LEN, ENDIAN)  // system memory write
#define JpuReadMem(devctx, ADDR, DATA, LEN, ENDIAN) \
  jdi_read_memory(devctx, ADDR, DATA, LEN, ENDIAN)  // system memory write

typedef enum {
  JPG_START_PIC = 0,
  JPG_START_INIT,
  JPG_START_PARTIAL,
  JPG_ENABLE_START_PIC = 4,
  JPG_DEC_SLICE_ENABLE_START_PIC = 5
} JpgStartCmd;

typedef enum {
  INST_CTRL_IDLE = 0,
  INST_CTRL_LOAD = 1,
  INST_CTRL_RUN = 2,
  INST_CTRL_PAUSE = 3,
  INST_CTRL_ENC = 4,
  INST_CTRL_PIC_DONE = 5,
  INST_CTRL_SLC_DONE = 6
} InstCtrlStates;

typedef struct {
  Uint32 tag;
  Uint32 type;
  int count;
  int offset;
} TAG;

enum { JFIF = 0, JFXX_JPG = 1, JFXX_PAL = 2, JFXX_RAW = 3, EXIF_JPG = 4 };

typedef struct {
  int PicX;
  int PicY;
  int BitPerSample[3];
  int Compression;       // 1 for uncompressed / 6 for compressed(jpeg)
  int PixelComposition;  // 2 for RGB / 6 for YCbCr
  int SamplePerPixel;
  int PlanrConfig;     // 1 for chunky / 2 for planar
  int YCbCrSubSample;  // 00020002 for YCbCr 4:2:0 / 00020001 for YCbCr 4:2:2
  Uint32 JpegOffset;
  Uint32 JpegThumbSize;
} EXIF_INFO;

#define init_get_bits(CTX, BUFFER, SIZE) JpuGbuInit(CTX, BUFFER, SIZE)
#define show_bits(CTX, NUM) JpuGguShowBit(CTX, NUM)
#define get_bits(CTX, NUM) JpuGbuGetBit(CTX, NUM)
#define get_bits_left(CTX) JpuGbuGetLeftBitCount(CTX)
#define get_bits_count(CTX) JpuGbuGetUsedBitCount(CTX)

extern Uint8 sJpuCompInfoTable[5][24];
extern Uint8 sJpuCompInfoTable_EX[5][24];

#ifdef __cplusplus
extern "C" {
#endif
JpgRet GetJpgInstance(JdiDeviceCtx devctx, JpgInst **ppInst);
void FreeJpgInstance(JpgInst *pJpgInst);
JpgRet CheckJpgInstValidity(JpgInst *pci);
JpgRet CheckJpgDecOpenParam(JpgDecOpenParam *pop);

int JpuGbuInit(vpu_getbit_context_t *ctx, BYTE *buffer, int size);
int JpuGbuGetUsedBitCount(vpu_getbit_context_t *ctx);
int JpuGbuGetLeftBitCount(vpu_getbit_context_t *ctx);
unsigned int JpuGbuGetBit(vpu_getbit_context_t *ctx, int bit_num);
unsigned int JpuGguShowBit(vpu_getbit_context_t *ctx, int bit_num);

int JpegDecodeHeader(JpgDecInfo *jpg, JdiDeviceCtx devctx);
int JpgDecQMatTabSetUp(JpgDecInfo *jpg, JdiDeviceCtx devctx, int instRegIndex);
int JpgDecHuffTabSetUp(JpgDecInfo *jpg, JdiDeviceCtx devctx, int instRegIndex);
int JpgDecHuffTabSetUp_12b(JpgDecInfo *jpg, JdiDeviceCtx devctx,
                           int instRegIndex);
void JpgDecGramSetup(JpgDecInfo *jpg, JdiDeviceCtx devctx, int instRegIndex);

JpgRet CheckJpgEncOpenParam(JpgEncOpenParam *pop, JPUCap *cap);
JpgRet CheckJpgEncParam(JpgEncHandle handle, JpgEncParam *param);
int JPUEncGetHuffTable(char *huffFileName, EncMjpgParam *param, int prec);
int JPUEncGetQMatrix(char *qMatFileName, EncMjpgParam *param);
int JpgEncLoadHuffTab(JpgInst *pJpgInst, int instRegIndex);
int JpgEncLoadHuffTab_12b(JpgInst *pJpgInst, int instRegIndex);
int JpgEncLoadQMatTab(JpgInst *pJpgInst, int instRegIndex);
int JpgEncEncodeHeader(JpgEncHandle handle, JpgEncParamSet *para);

JpgRet JpgEnterLock(JdiDeviceCtx devctx);
JpgRet JpgLeaveLock(JdiDeviceCtx devctx);
JpgRet JpgSetClockGate(Uint32 on);

JpgInst *GetJpgPendingInstEx(JdiDeviceCtx devctx, Uint32 instIdx);
void SetJpgPendingInstEx(JpgInst *inst, JdiDeviceCtx devctx, Uint32 instIdx);
void ClearJpgPendingInstEx(JdiDeviceCtx devctx, Uint32 instIdx);

Uint32 GetDec8bitBusReqNum(FrameFormat iFormat, PackedFormat oPackMode);
Uint32 GetDec12bitBusReqNum(FrameFormat iFormat, PackedFormat oPackMode);
Uint32 GetEnc8bitBusReqNum(PackedFormat iPackMode, FrameFormat oFormat);
Uint32 GetEnc12bitBusReqNum(PackedFormat iPackMode, FrameFormat oFormat);

#ifdef __cplusplus
}
#endif

#endif  // endif JPUAPI_UTIL_H_INCLUDED
