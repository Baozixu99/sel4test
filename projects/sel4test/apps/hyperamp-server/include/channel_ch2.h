/*
 * CH2 远程内存访问通道 — 头文件
 *
 * 封装 monkey-mnemosyne 的 mnemosyne_api.h，
 * 提供与 channel_ch0.h / channel_ch1.h 一致的初始化和轮询接口。
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CHANNEL_CH2_H
#define CHANNEL_CH2_H

#include "channel.h"

/* CH2 队列容量（与 CH1 相同，由 1MB 物理内存决定） */
#define CH2_QUEUE_CAPACITY  253

/**
 * @brief 初始化 CH2 远程内存访问子系统
 *
 * 内部调用 mnemosyne_engine_init_with_vaddrs()，使用外部传入的虚拟地址
 * 完成 HyperAmpBridge 初始化。
 *
 * @param ctx  已经通过 channel_init() 初始化的通道上下文
 * @param tx_va  CH2 TX queue 虚拟地址（来自 seL4_GetMR(8)）
 * @param rx_va  CH2 RX queue 虚拟地址（来自 seL4_GetMR(9)）
 * @param data_va CH2 Data region 虚拟地址（来自 seL4_GetMR(10)）
 * @return HYPERAMP_OK 成功, 负值失败
 */
int ch2_init(ChannelContext *ctx,
             seL4_Word tx_va, seL4_Word rx_va, seL4_Word data_va);

/**
 * @brief CH2 轮询处理
 *
 * 调用 mnemosyne_engine_run_hyperamp_once()。
 * 当前该函数为 no-op（monkey-mnemosyne 的收发由 Session 自驱动），
 * 保留此接口是为了与 CH0/CH1 的主循环结构保持一致。
 *
 * @param ctx  通道上下文
 * @return HYPERAMP_OK 有工作, HYPERAMP_AGAIN 无工作
 */
int ch2_process_message(ChannelContext *ctx);

#endif /* CHANNEL_CH2_H */
