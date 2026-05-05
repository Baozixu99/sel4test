/*
 * CH0 通道处理模块 — 图片/文本加解密与验证服务
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CHANNEL_CH0_H
#define CHANNEL_CH0_H

#include "channel.h"

/**
 * @brief 处理 CH0 接收队列中的一条消息
 *
 * 从 CH0 RX Queue 出队一条消息，根据消息类型分发到
 * Bulk 处理、Service Call 或代理消息处理函数。
 * 每次调用只处理一条消息，供主循环轮询调用。
 *
 * @param ctx CH0 通道上下文
 * @return HYPERAMP_OK=成功处理一条消息,
 *         HYPERAMP_AGAIN=队列为空,
 *         HYPERAMP_ERROR=错误
 */
int ch0_process_message(ChannelContext *ctx);

#endif /* CHANNEL_CH0_H */
