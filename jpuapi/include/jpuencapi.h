/*
 * Copyright (C) 2022 ASR Micro Limited
 * All Rights Reserved.
 */

#include "jputypes.h"

#ifndef JPUENC_H_INCLUDED
#define JPUENC_H_INCLUDED
#ifdef __cplusplus
extern "C" {
#endif

JpgRet AsrJpuEncOpen(void** handle, EncOpenParam* param);
JpgRet AsrJpuEncSetParam(void* handle, Uint32 parameterIndex, void* value);
JpgRet AsrJpuEncStartOneFrame(void* handle, FrameBufferInfo* frameBuffer,
                              ImageBufferInfo* jpegImageBuffer);
JpgRet AsrJpuEncClose(void* handle);

#ifdef __cplusplus
}
#endif

#endif