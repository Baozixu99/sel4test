/*
 * 多通道 HyperAMP 通道抽象层实现
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include "channel.h"

/* ==================== 平台物理地址定义 ==================== */

#if defined(CONFIG_PLAT_IMX8MP_EVK) || defined(CONFIG_PLAT_RK3588)
    #define SHM_BASE_PADDR      0x7E000000UL
#elif defined(CONFIG_PLAT_PHYTIUM_PI)
    #define SHM_BASE_PADDR      0xDE000000UL
#else
    #error "Unknown Platform! Please define addresses for this board."
#endif

/* 通道物理地址偏移量 */
#define CH0_PADDR_OFFSET    0x000000UL
#define CH1_PADDR_OFFSET    0x200000UL

/* ==================== 通道初始化 ==================== */

int channel_init(ChannelContext *ctx, int ch_id,
                 seL4_Word tx_va, seL4_Word rx_va, seL4_Word data_va,
                 uint16_t capacity)
{
    if (!ctx) return HYPERAMP_ERROR;

    /* 校验地址有效性 */
    if (!tx_va || !rx_va || !data_va) {
        printf("[CH%d] 错误：IPC 传入的虚拟地址无效 (tx=%p, rx=%p, data=%p)\n",
               ch_id, (void *)tx_va, (void *)rx_va, (void *)data_va);
        return HYPERAMP_ERROR;
    }

    /* 初始化上下文 */
    ctx->channel_id     = ch_id;
    ctx->tx_queue       = (volatile HyperampShmQueue *)tx_va;
    ctx->rx_queue       = (volatile HyperampShmQueue *)rx_va;
    ctx->data_region    = (volatile void *)data_va;
    ctx->queue_capacity = capacity;
    ctx->initialized    = 0;

    printf("[CH%d] 通道初始化开始\n", ch_id);
    printf("[CH%d]   TX Queue: %p\n", ch_id, (void *)ctx->tx_queue);
    printf("[CH%d]   RX Queue: %p\n", ch_id, (void *)ctx->rx_queue);
    printf("[CH%d]   Data Region: %p\n", ch_id, (void *)ctx->data_region);
    printf("[CH%d]   Capacity: %u\n", ch_id, capacity);

    /* 计算物理地址 */
    uint64_t phy_offset = (ch_id == CHANNEL_ID_CH1) ? CH1_PADDR_OFFSET : CH0_PADDR_OFFSET;
    uint64_t tx_phy = SHM_BASE_PADDR + phy_offset;
    uint64_t rx_phy = SHM_BASE_PADDR + phy_offset + 0x1000UL;

    /* 配置 TX Queue */
    HyperampQueueConfig tx_config = {
        .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
        .capacity   = capacity,
        .block_size = DEFAULT_BLOCK_SIZE,
        .phy_addr   = tx_phy,
        .virt_addr  = (uint64_t)ctx->tx_queue,
    };

    /* 配置 RX Queue */
    HyperampQueueConfig rx_config = {
        .map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
        .capacity   = capacity,
        .block_size = DEFAULT_BLOCK_SIZE,
        .phy_addr   = rx_phy,
        .virt_addr  = (uint64_t)ctx->rx_queue,
    };

    /* 初始化 TX Queue (seL4 作为 creator) */
    if (hyperamp_queue_init(ctx->tx_queue, &tx_config, 1) != HYPERAMP_OK) {
        printf("[CH%d] 错误：TX Queue 初始化失败\n", ch_id);
        return HYPERAMP_ERROR;
    }
    hyperamp_cache_invalidate((volatile void *)ctx->tx_queue, 64);
    printf("[CH%d] TX Queue 初始化完成\n", ch_id);

    /* 初始化 RX Queue (seL4 作为 creator) */
    if (hyperamp_queue_init(ctx->rx_queue, &rx_config, 1) != HYPERAMP_OK) {
        printf("[CH%d] 错误：RX Queue 初始化失败\n", ch_id);
        return HYPERAMP_ERROR;
    }
    hyperamp_cache_invalidate((volatile void *)ctx->rx_queue, 64);
    printf("[CH%d] RX Queue 初始化完成\n", ch_id);

    ctx->initialized = 1;
    printf("[CH%d] 通道初始化完成\n", ch_id);
    return HYPERAMP_OK;
}

/* ==================== cache 安全读写接口 ==================== */

int shm_read_buffer(ChannelContext *ctx, void *buf, size_t max_len,
                    size_t *actual_len)
{
    if (!ctx || !ctx->initialized || !buf) return HYPERAMP_ERROR;

    /* 读前 invalidate：确保读到对端（Linux）写入的最新数据 */
    hyperamp_cache_invalidate((volatile void *)ctx->rx_queue, 64);

    return hyperamp_queue_dequeue(ctx->rx_queue, ZONE_ID_SEL4,
                                  buf, max_len, actual_len,
                                  ctx->data_region);
}

int shm_write_buffer(ChannelContext *ctx, const void *buf, size_t len)
{
    if (!ctx || !ctx->initialized || !buf || len == 0) return HYPERAMP_ERROR;

    return hyperamp_queue_enqueue(ctx->tx_queue, ZONE_ID_SEL4,
                                  buf, len, ctx->data_region);
    /* 注：enqueue 内部已执行 cache_clean，无需额外操作 */
}
