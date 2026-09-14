/*
 * RDMA_demo —— 精简版 debug.h（替代 NCCL 的 src/include/debug.h）
 *
 * 背景：
 *   nccl_hack_rdma/src/nsibverbs/ 下的实现（qp.cc / device.cc 等）会 include "debug.h"，
 *   并使用其中的 WARN(...) / INFO(FLAGS, ...) 宏。原版 debug.h 依赖 nccl.h、nccl_common.h
 *   以及 ncclDebugLog() 等 NCCL 内部设施，会把整个 NCCL 主体拉进来。
 *
 * 本文件的目的：
 *   在保持 nsibverbs 源码「原样 copy、零改动」的前提下，提供一个不依赖 NCCL 的最小 debug.h，
 *   使 nsibverbs 能够脱离 NCCL 主体、在无 GPU / 无 RDMA 库的环境下独立编译。
 *
 * 与原版的行为差异：
 *   - WARN(...)        : 始终打印到 stderr。
 *   - INFO(FLAGS, ...) : 仅当设置了环境变量 RDMA_DEMO_VERBOSE 时才打印（默认静默，避免淹没 demo 输出）。
 *                        原版中的 FLAGS（如 NCCL_INIT）用于日志分类，这里被忽略。
 *   - TRACE / VERSION  : 空实现。
 */
#ifndef RDMA_DEMO_DEBUG_H_
#define RDMA_DEMO_DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

/* 原版 INFO(FLAGS, ...) 的 FLAGS 取值；此处仅为兼容 nsibverbs 源码中的调用而定义，数值无实际意义。 */
#define NCCL_INIT       0
#define NCCL_NET        0
#define NCCL_GRAPH      0
#define NCCL_TUNING     0
#define NCCL_ENV        0
#define NCCL_ALLOC      0
#define NCCL_CALL       0
#define NCCL_PROXY      0
#define NCCL_BOOTSTRAP  0
#define NCCL_ALL        0

/* 兼容原版 debug.h 中的日志级别枚举（nsibverbs 未直接使用，保留以防其他引用）。 */
enum ncclDebugLogLevel {
  NCCL_LOG_NONE    = 0,
  NCCL_LOG_VERSION = 1,
  NCCL_LOG_WARN    = 2,
  NCCL_LOG_INFO    = 3,
  NCCL_LOG_TRACE   = 4
};

#define WARN(...)                                                          \
  do {                                                                     \
    fprintf(stderr, "[nsibverbs][WARN] " __VA_ARGS__);                     \
    fputc('\n', stderr);                                                   \
  } while (0)

#define INFO(FLAGS, ...)                                                   \
  do {                                                                     \
    if (getenv("RDMA_DEMO_VERBOSE") != NULL) {                             \
      fprintf(stderr, "[nsibverbs][INFO] " __VA_ARGS__);                   \
      fputc('\n', stderr);                                                 \
    }                                                                      \
  } while (0)

#define TRACE(...)   do {} while (0)
#define VERSION(...) do {} while (0)

#endif  /* RDMA_DEMO_DEBUG_H_ */
