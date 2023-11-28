# Copyright (C) 2019 ASR Micro Limited
# All Rights Reserved.
cmake_minimum_required (VERSION 3.1.0)
project (jpu-test)

#set(CMAKE_SYSTEM_NAME Linux)
#set(CMAKE_SYSTEM_PROCESSOR riscv64)

#set(BUILD_SHARED_LIBS off)
#set(CMAKE_EXE_LINKER_FLAGS "-static")
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

#set(CROSS_COMPILE riscv64-buildroot-linux-gnu-)
#set(CMAKE_CROSSCOMPILING True)
#set(CMAKE_C_COMPILER ${CROSS_COMPILE}gcc)
#set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
#set(CMAKE_STRIP ${CROSS_COMPILE}strip)
#set(CMAKE_AR ${CROSS_COMPILE}ar)
#set(CMAKE_RANLIB ${CROSS_COMPILE}ranlib)




include_directories(
	${PROJECT_SOURCE_DIR}/jpuapi/
	${PROJECT_SOURCE_DIR}/jpuapi/include/
	${PROJECT_SOURCE_DIR}/sample/helper/
    ${PROJECT_SOURCE_DIR}/sample/dmabufheap/
)

set(SRC 
${PROJECT_SOURCE_DIR}/jpuapi/jpuapi.c 
${PROJECT_SOURCE_DIR}/jpuapi/jpuapifunc.c
${PROJECT_SOURCE_DIR}/jpuapi/jdi.c
${PROJECT_SOURCE_DIR}/jpuapi/jpuencapi.c
${PROJECT_SOURCE_DIR}/jpuapi/jpudecapi.c

)
add_library(jpu STATIC ${SRC})

set(SAMPLE_SRC 
${PROJECT_SOURCE_DIR}/sample/helper/jpulog.c
${PROJECT_SOURCE_DIR}/sample/helper/bitstreamwriter.c
${PROJECT_SOURCE_DIR}/sample/helper/bitstreamfeeder.c
${PROJECT_SOURCE_DIR}/sample/helper/yuv_feeder.c 
${PROJECT_SOURCE_DIR}/sample/helper/datastructure.c
${PROJECT_SOURCE_DIR}/sample/helper/jpuhelper.c
${PROJECT_SOURCE_DIR}/sample/helper/jpulog.c 
${PROJECT_SOURCE_DIR}/sample/helper/platform.c
${PROJECT_SOURCE_DIR}/sample/helper/bsfeeder_fixedsize_impl.c
)

set(DMAHEAP_SRC 
${PROJECT_SOURCE_DIR}/sample/dmabufheap/BufferAllocator.cpp
${PROJECT_SOURCE_DIR}/sample/dmabufheap/BufferAllocatorWrapper.cpp
)

# Build object library.
add_library(dma_obj OBJECT "${DMAHEAP_SRC}")

add_executable(jpu_enc_test sample/main_enc_test.c ${SAMPLE_SRC})
target_link_libraries(jpu_enc_test jpu dma_obj)

add_executable(jpu_dec_test sample/main_dec_test.c ${SAMPLE_SRC})
target_link_libraries(jpu_dec_test jpu dma_obj)

