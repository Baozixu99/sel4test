/*
 * CH1 通道处理模块 — 网络代理
 *
 * 支持双 Session 分类转发：
 *   Session A (port 8888) — 文本/小文件加密结果
 *   Session B (port 8889) — 图片/大文件 Bulk 加密结果
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CHANNEL_CH1_H
#define CHANNEL_CH1_H

#include "channel.h"

/* ==================== 转发描述头 ==================== */

/* 转发描述头魔数 "HFWD" */
#define FORWARD_HEADER_MAGIC  0x48465744

/**
 * @brief 跨通道转发描述头
 *
 * 在每次转发数据之前先发送此描述头，让 Linux 接收端知道：
 * - 这段数据属于哪个服务（加密/验签加密/字段验证加密）
 * - 是 Bulk 还是普通消息
 * - 后续数据的总长度（用于分文件保存）
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 0x48465744 ("HFWD") */
    uint8_t  service_id;    /* SERVICE_ENCRYPT=1, SERVICE_VERIFY_ENCRYPT=4, ... */
    uint8_t  is_bulk;       /* 0=普通消息, 1=Bulk 传输 */
    uint16_t reserved;      /* 对齐填充 */
    uint32_t total_len;     /* 后续数据的总字节数 */
} ForwardHeader;

/* ==================== CH1 初始化与消息处理 ==================== */

/**
 * @brief 初始化 CH1 网络代理子系统
 *
 * 设置 front 引擎，并创建两个 Session：
 *   Session A → 192.168.137.2:8888 （文本数据）
 *   Session B → 192.168.137.2:8889 （图片/大文件数据）
 */
int ch1_init(ChannelContext *ctx);

/**
 * @brief 处理 CH1 接收队列中的消息
 */
int ch1_process_message(ChannelContext *ctx);

/* ==================== 跨通道转发接口 ==================== */

/**
 * @brief 检查文本 Session (port 8888) 是否就绪
 */
int ch1_is_text_ready(void);

/**
 * @brief 检查 Bulk Session (port 8889) 是否就绪
 */
int ch1_is_bulk_ready(void);

/**
 * @brief 兼容旧接口：检查任一 Session 是否就绪
 */
int ch1_is_ready(void);

/**
 * @brief 通过文本 Session (port 8888) 转发数据
 *
 * 用于普通消息（非 Bulk）的加密结果转发。
 * 自动在数据前添加 ForwardHeader。
 *
 * @param data       待转发的数据
 * @param len        数据长度
 * @param service_id 服务类型（SERVICE_ENCRYPT 等）
 */
int ch1_forward_text_data(const uint8_t *data, size_t len, uint8_t service_id);

/**
 * @brief 通过 Bulk Session (port 8889) 转发数据
 *
 * 用于 Bulk 传输的加密结果转发。
 * 自动在数据前添加 ForwardHeader。
 *
 * @param data       待转发的数据
 * @param len        数据长度
 * @param service_id 服务类型（SERVICE_ENCRYPT 等）
 */
int ch1_forward_bulk_data(const uint8_t *data, size_t len, uint8_t service_id, uint32_t total_length);

/**
 * @brief 兼容旧接口：通过默认 Session 转发数据（走文本端口）
 */
int ch1_forward_data(const uint8_t *data, size_t len);

/**
 * @brief 通过 Bulk Session 转发原始数据 (无 ForwardHeader)
 */
int ch1_forward_bulk_raw_data(const uint8_t *data, size_t len);

#endif /* CHANNEL_CH1_H */
