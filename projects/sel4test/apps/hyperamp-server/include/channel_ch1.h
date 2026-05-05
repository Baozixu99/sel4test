/*
 * CH1 通道处理模块 — 网络代理
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CHANNEL_CH1_H
#define CHANNEL_CH1_H

#include "channel.h"

/**
 * @brief 初始化 CH1 网络代理子系统
 *
 * 设置 front 引擎的全局变量，并初始化 FrontendEngine、
 * SessionPool、IoT 设备等子系统。
 *
 * @param ctx CH1 通道上下文（已初始化完成的通道）
 * @return 0 成功, -1 失败
 */
int ch1_init(ChannelContext *ctx);

/**
 * @brief 处理 CH1 接收队列中的消息
 *
 * 封装 frontend_engine_run_hyperamp_once() 的等价逻辑，
 * 每次调用执行一轮消息处理（出队 → 协议解析 → 会话处理）。
 *
 * @param ctx CH1 通道上下文
 * @return HYPERAMP_OK=成功, HYPERAMP_AGAIN=无消息, HYPERAMP_ERROR=错误
 */
int ch1_process_message(ChannelContext *ctx);

#endif /* CHANNEL_CH1_H */
