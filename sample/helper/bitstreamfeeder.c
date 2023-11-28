/*
 /*
  * Copyright (C) 2019 ASR Micro Limited
  * All Rights Reserved.
  */

#include "jpuapi.h"
#include "main_helper.h"

#if 0
typedef struct {
    FeedingMethod   method;
    Uint8*          remainData;
    Uint32          remainDataSize;
    Uint32          remainOffset;
    void*           actualFeeder;
    Uint32          room;
    BOOL            eos;
    EndianMode      endian;
} BitstreamFeeder;

void* BSFeederFixedSize_Create(const char* path);
BOOL BSFeederFixedSize_Destroy(void* feeder);
Int32 BSFeederFixedSize_Act(void* feeder,ImageBufferInfo *bsBuffer);
void BSFeederFixedSize_SetFeedingSize(void* feeder, Uint32  feedingSize);
#endif

#ifdef USE_FFMPEG
extern void* BSFeederFrameSize_Create(const char* path);
extern BOOL BSFeederFrameSize_Destroy(void* feeder);
extern Int32 BSFeederFrameSize_Act(void* feeder, BSChunk* packet);
#endif

/**
 * Abstract Bitstream Feeader Functions
 */
BSFeeder BitstreamFeeder_Create(const char* path, FeedingMethod method,
                                EndianMode endian) {
  BitstreamFeeder* handle = NULL;
  void* feeder = NULL;

  feeder = BSFeederFixedSize_Create(path);

  if (feeder != NULL) {
    if ((handle = (BitstreamFeeder*)malloc(sizeof(BitstreamFeeder))) == NULL) {
      JLOG(ERR, "%s:%d Failed to allocate memory\n", __FUNCTION__, __LINE__);
      return NULL;
    }
    handle->actualFeeder = feeder;
    handle->method = method;
    handle->remainData = NULL;
    handle->remainDataSize = 0;
    handle->eos = FALSE;
    handle->endian = endian;
  }

  return (BSFeeder)handle;
}

Uint32 BitstreamFeeder_Act(BSFeeder feeder, JpgDecHandle handle,
                           ImageBufferInfo* bsBuffer) {
  BitstreamFeeder* bsf = (BitstreamFeeder*)feeder;
  Int32 feedingSize = 0;

  if (bsf == NULL) {
    JLOG(ERR, "%s:%d Null handle\n", __FUNCTION__, __LINE__);
    return 0;
  }

  feedingSize = BSFeederFixedSize_Act(bsf->actualFeeder, bsBuffer);
  return feedingSize;
}

BOOL BitstreamFeeder_IsEos(BSFeeder feeder) {
  BitstreamFeeder* bsf = (BitstreamFeeder*)feeder;

  if (bsf == NULL) {
    JLOG(ERR, "%s:%d Null handle\n", __FUNCTION__, __LINE__);
    return FALSE;
  }

  return bsf->eos;
}

BOOL BitstreamFeeder_Destroy(BSFeeder feeder) {
  BitstreamFeeder* bsf = (BitstreamFeeder*)feeder;

  if (bsf == NULL) {
    return FALSE;
  }

  switch (bsf->method) {
    case FEEDING_METHOD_FIXED_SIZE:
      BSFeederFixedSize_Destroy(bsf->actualFeeder);
      break;
    case FEEDING_METHOD_FRAME_SIZE:
#ifdef USE_FFMPEG
      BSFeederFrameSize_Destroy(bsf->actualFeeder);
#endif
      break;
    default:
      JLOG(ERR, "%s:%d Invalid method(%d)\n", __FUNCTION__, __LINE__,
           bsf->method);
      break;
  }

  if (bsf->remainData) {
    free(bsf->remainData);
  }

  free(bsf);

  return TRUE;
}
