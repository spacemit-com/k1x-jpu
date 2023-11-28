/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef __DATA_STRUCTURE_H__
#define __DATA_STRUCTURE_H__

#include "jputypes.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/************************************************************************/
/* Queue                                                                */
/************************************************************************/
typedef struct {
  void* data;
} QueueData;

typedef struct {
  Uint8* buffer;
  Uint32 size;
  Uint32 itemSize;
  Uint32 count;
  Uint32 front;
  Uint32 rear;
  JpuMutex lock;
} Queue;

Queue* Queue_Create(Uint32 itemCount, Uint32 itemSize);

Queue* Queue_Create_With_Lock(Uint32 itemCount, Uint32 itemSize);

void Queue_Destroy(Queue* queue);

/**
 * \brief       Enqueue with deep copy
 */
BOOL Queue_Enqueue(Queue* queue, void* data);

/**
 * \brief       Caller has responsibility for releasing the returned data
 */
void* Queue_Dequeue(Queue* queue);

void Queue_Flush(Queue* queue);

void* Queue_Peek(Queue* queue);

Uint32 Queue_Get_Cnt(Queue* queue);

/**
 * \brief       @dstQ is NULL, it allocates Queue structure and then copy from
 * @srcQ.
 */
Queue* Queue_Copy(Queue* dstQ, Queue* srcQ);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __DATA_STRUCTURE_H__ */
