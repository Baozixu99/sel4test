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

/**
 * @brief 检查 CH1 网络代理 Session 是否已就绪（已连接）
 *
 * @return 1=已就绪可发送, 0=未就绪
 */
int ch1_is_ready(void);

/**
 * @brief 通过 CH1 网络代理转发数据（供 CH0 跨通道调用）
 *
 * 将 CH0 安全处理后的结果通过 CH1 的网络代理 Session 发送，
 * 最终由 Linux 侧 HighSpeedCProxy 接收并通过真实网络对外转发。
 *
 * @param data 待转发的数据
 * @param len  数据长度
 * @return HYPERAMP_OK=成功, HYPERAMP_ERROR=失败（Session 未就绪等）
 */
int ch1_forward_data(const uint8_t *data, size_t len);

#endif /* CHANNEL_CH1_H */
