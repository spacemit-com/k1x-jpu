/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#include <linux/dma-buf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "BufferAllocatorWrapper.h"
#include "main_helper.h"

#define MAX_FEEDING_SIZE 0x400000 /* 4MBytes */

typedef struct FeederFixedContext {
  FILE* fp;
  BOOL eos;
} FeederFixedContext;

void* BSFeederFixedSize_Create(const char* path) {
  FILE* fp = NULL;
  FeederFixedContext* context = NULL;

  if ((fp = fopen(path, "rb")) == NULL) {
    JLOG(ERR, "%s:%d failed to open %s\n", __FUNCTION__, __LINE__, path);
    return NULL;
  }

  context = (FeederFixedContext*)malloc(sizeof(FeederFixedContext));
  if (context == NULL) {
    JLOG(ERR, "%s:%d failed to allocate memory\n", __FUNCTION__, __LINE__);
    fclose(fp);
    return NULL;
  }

  context->fp = fp;
  context->eos = FALSE;

  return (void*)context;
}

// lint -e482
BOOL BSFeederFixedSize_Destroy(void* feeder) {
  FeederFixedContext* context = (FeederFixedContext*)feeder;

  if (context == NULL) {
    JLOG(ERR, "%s:%d Null handle\n", __FUNCTION__, __LINE__);
    return FALSE;
  }

  if (context->fp) fclose(context->fp);

  free(context);

  return TRUE;
}
// int +e482

Int32 BSFeederFixedSize_Act(void* feeder, ImageBufferInfo* bsBuffer) {
  FeederFixedContext* context = (FeederFixedContext*)feeder;
  Uint32 nRead;
  Uint32 size;
  void* data;

  if (context == NULL) {
    JLOG(ERR, "%s:%d Null handle\n", __FUNCTION__, __LINE__);
    return 0;
  }

  if (context->eos == TRUE) {
    return 0;
  }
  data = mmap(NULL, bsBuffer->dmaBuffer.size, PROT_READ | PROT_WRITE,
              MAP_SHARED, bsBuffer->dmaBuffer.fd, 0);
  size = bsBuffer->dmaBuffer.size;

  do {
    nRead = fread(data, 1, size, context->fp);
    if ((Int32)nRead < 0) {
      JLOG(ERR, "%s:%d failed to read bitstream\n", __FUNCTION__, __LINE__);
      return 0;
    } else if (nRead < size) {
      context->eos = TRUE;
    }
  } while (FALSE);

  munmap(data, bsBuffer->dmaBuffer.size);

  return nRead;
}
