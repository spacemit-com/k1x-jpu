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

JpgRet AsrJpuDecOpen(void** handle, DecOpenParam* param);
JpgRet AsrJpuDecSetParam(void* handle, Uint32 parameterIndex, void* value);
JpgRet AsrJpuDecGetInitialInfo(void* handle, ImageBufferInfo* jpegImageBuffer,
                               JpgDecInitialInfo* info);
JpgRet AsrJpuDecStartOneFrame(void* handle, FrameBufferInfo* frameBuffer,
                              ImageBufferInfo* jpegImageBuffer);
JpgRet AsrJpuDecClose(void* handle);

#ifdef __cplusplus
}
#endif

#endif