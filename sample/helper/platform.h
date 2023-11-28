/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef __JPU_PLATFORM_H__
#define __JPU_PLATFORM_H__

#include "jputypes.h"
/************************************************************************/
/* JpuMutex                                                                */
/************************************************************************/
typedef void* JpuMutex;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
extern void MSleep(Uint32);

extern JpuMutex JpuMutex_Create(void);

extern void JpuMutex_Destroy(JpuMutex handle);

extern BOOL JpuMutex_Lock(JpuMutex handle);

extern BOOL JpuMutex_Unlock(JpuMutex handle);

Uint32 GetRandom(Uint32 start, Uint32 end);
#ifdef __cplusplus
}
#endif /* __cplusplus */

/************************************************************************/
/* JpuThread                                                               */
/************************************************************************/
typedef void* JpuThread;
typedef void (*JpuThreadRunner)(void*);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern JpuThread JpuThread_Create(JpuThreadRunner func, void* arg);

extern BOOL JpuThread_Join(JpuThread thread);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __JPU_PLATFORM_H__ */
