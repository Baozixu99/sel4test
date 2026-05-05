/*
 * 多通道 HyperAMP 通道抽象层
 *
 * 提供统一的通道上下文结构和 cache 安全的读写接口，
 * 用于支持 CH0（加解密/验证）和 CH1（网络代理）的并发轮询。
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include <stddef.h>
#include <sel4/sel4.h>
#include "hyperamp_shm_queue.h"

/* ==================== 通道 ID 常量 ==================== */

#define CHANNEL_ID_CH0      0
#define CHANNEL_ID_CH1      1
#define CHANNEL_ID_CH2      2  /* 预留 */

/* Zone ID */
#define ZONE_ID_LINUX       0
#define ZONE_ID_SEL4        1

/* 默认队列参数 */
#define CH0_QUEUE_CAPACITY  256
#define CH1_QUEUE_CAPACITY  253   /* CH1 总共 1MB; 数据区 = 1MB - 8KB = 254 blocks; cap <= 253 */
#define DEFAULT_BLOCK_SIZE  4096

/* ==================== 通道上下文结构 ==================== */

/**
 * @brief 统一的通道上下文结构
 *
 * 每个通道独立持有自己的队列指针、数据区指针和配置参数，
 * 确保不同通道之间完全隔离。
 */
typedef struct {
    int channel_id;                        /* 通道编号: 0=CH0, 1=CH1, 2=CH2 */
    volatile HyperampShmQueue *tx_queue;   /* seL4 → Linux 发送队列 */
    volatile HyperampShmQueue *rx_queue;   /* Linux → seL4 接收队列 */
    volatile void *data_region;            /* 共享数据区基址 */
    uint16_t queue_capacity;               /* 队列容量 */
    int initialized;                       /* 初始化完成标志 */
} ChannelContext;

/* ==================== 通道操作接口 ==================== */

/**
 * @brief 初始化通道
 *
 * 根据 IPC buffer 传入的虚拟地址，初始化队列结构（seL4 作为 creator）。
 *
 * @param ctx       通道上下文指针
 * @param ch_id     通道编号 (0/1/2)
 * @param tx_va     TX 队列虚拟地址（来自 IPC msg）
 * @param rx_va     RX 队列虚拟地址（来自 IPC msg）
 * @param data_va   数据区虚拟地址（来自 IPC msg）
 * @param capacity  队列容量
 * @return 0 成功, -1 失败
 */
int channel_init(ChannelContext *ctx, int ch_id,
                 seL4_Word tx_va, seL4_Word rx_va, seL4_Word data_va,
                 uint16_t capacity);

/**
 * @brief 从通道 RX 队列读取消息（cache 安全）
 *
 * 内部自动执行 invalidate → dequeue 流程。
 *
 * @param ctx        通道上下文
 * @param buf        接收缓冲区
 * @param max_len    缓冲区最大长度
 * @param actual_len 输出：实际读取长度
 * @return HYPERAMP_OK=有消息, HYPERAMP_AGAIN=队列空, HYPERAMP_ERROR=错误
 */
int shm_read_buffer(ChannelContext *ctx, void *buf, size_t max_len,
                    size_t *actual_len);

/**
 * @brief 向通道 TX 队列写入消息（cache 安全）
 *
 * 内部自动执行 enqueue → clean 流程。
 *
 * @param ctx  通道上下文
 * @param buf  发送数据
 * @param len  数据长度
 * @return HYPERAMP_OK=成功, HYPERAMP_AGAIN=队列满, HYPERAMP_ERROR=错误
 */
int shm_write_buffer(ChannelContext *ctx, const void *buf, size_t len);

#endif /* CHANNEL_H */
