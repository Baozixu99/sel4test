/*
 * CH1 通道处理模块实现 — 网络代理（双 Session 分类转发）
 *
 * Session A (port 8888): 文本/小文件加密结果
 * Session B (port 8889): 图片/大文件 Bulk 加密结果
 *
 * 每次转发前自动添加 ForwardHeader，接收端据此分文件保存。
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* front 头文件中有空前向声明，抑制对应的 -Werror */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "channel_ch1.h"

/* 引入 front 引擎的接口和全局变量 */
#include "engine.h"
#include "frontend_api.h"
#include "senario_test.h"

/* front_engine_run_hyperamp_once 未在 engine.h 中声明，手动补充 */
extern void frontend_engine_run_hyperamp_once(void);

/* front 引擎全局变量（定义在 front/src/engine.c 中） */
extern FrontendEngine *p_g_fr_eng;
extern FrontendEngine g_fr_eng;
extern volatile HyperampShmQueue *g_hyper_tx_queue;
extern volatile HyperampShmQueue *g_hyper_rx_queue;
extern volatile void *g_hyper_data_region;

/* ==================== 双 Session 管理 ==================== */

/* 文本 Session (port 8888) — 普通消息加密结果 */
static struct FrontendSession *g_ch1_text_session = NULL;

/* Bulk Session (port 8889) — 图片/大文件加密结果 */
static struct FrontendSession *g_ch1_bulk_session = NULL;

/* 转发请求序号（全局自增，用于日志追踪） */
static uint32_t g_forward_seq = 0;

/* ==================== Session 事件回调 ==================== */

/**
 * @brief 文本 Session 事件回调
 */
static void ch1_text_session_callback(struct FrontendSession *sess,
                                       FrontendSessionEvent event)
{
    switch (event) {
        case FRONTEND_SESS_EVENT_CONN:
            printf("[CH1] 文本 Session (8888) 已连接\n");
            break;
        case FRONTEND_SESS_EVENT_RECVDATA:
            printf("[CH1] 文本 Session 收到后端数据\n");
            break;
        case FRONTEND_SESS_EVENT_CLOSE:
            printf("[CH1] 文本 Session 已关闭\n");
            g_ch1_text_session = NULL;
            break;
        case FRONTEND_SESS_EVENT_ABNORMAL:
            printf("[CH1] 文本 Session 异常\n");
            break;
        default:
            break;
    }
}

/**
 * @brief Bulk Session 事件回调
 */
static void ch1_bulk_session_callback(struct FrontendSession *sess,
                                       FrontendSessionEvent event)
{
    switch (event) {
        case FRONTEND_SESS_EVENT_CONN:
            printf("[CH1] Bulk Session (8889) 已连接\n");
            break;
        case FRONTEND_SESS_EVENT_RECVDATA:
            printf("[CH1] Bulk Session 收到后端数据\n");
            break;
        case FRONTEND_SESS_EVENT_CLOSE:
            printf("[CH1] Bulk Session 已关闭\n");
            g_ch1_bulk_session = NULL;
            break;
        case FRONTEND_SESS_EVENT_ABNORMAL:
            printf("[CH1] Bulk Session 异常\n");
            break;
        default:
            break;
    }
}

/* ==================== CH1 初始化 ==================== */

int ch1_init(ChannelContext *ctx)
{
    if (!ctx || !ctx->initialized) {
        printf("[CH1] 错误：通道上下文未初始化\n");
        return HYPERAMP_ERROR;
    }

    printf("[CH1] 初始化网络代理子系统...\n");

    /*
     * 关键：在调用 frontend_engine_init() 之前，先设置 front 的全局队列变量。
     * 这样 frontend_engine_init() → engine_init_hyperamp_queue() 中
     * 会发现队列地址已有效，不会再从 IPC msg 重新读取（或读取到的值也一致）。
     *
     * 但实际上 engine_init_hyperamp_queue 会自己读 IPC msg[5..7] 并 re-init 队列。
     * 由于 channel_init() 已经初始化过 CH1 队列了，这里需要绕过重复初始化。
     *
     * 策略：直接设置全局变量，然后调用 front 的子系统初始化（跳过队列初始化步骤）。
     */

    /* 设置 front 的全局队列指针为 CH1 通道的队列 */
    g_hyper_tx_queue    = ctx->tx_queue;
    g_hyper_rx_queue    = ctx->rx_queue;
    g_hyper_data_region = ctx->data_region;

    /* 初始化 FrontendEngine 结构体 */
    memset(&g_fr_eng, 0, sizeof(g_fr_eng));
    p_g_fr_eng = &g_fr_eng;

    /* 初始化会话池 */
    p_g_fr_eng->sess_pool = malloc(sizeof(struct FrontendSessionPool));
    if (!p_g_fr_eng->sess_pool) {
        printf("[CH1] 错误：会话池内存分配失败\n");
        return HYPERAMP_ERROR;
    }

    int ret = frontend_high_speed_init_pool(p_g_fr_eng->sess_pool);
    if (ret != FRONTEND_PROXY_PROCESS_OK) {
        printf("[CH1] 错误：会话池初始化失败\n");
        free(p_g_fr_eng->sess_pool);
        return HYPERAMP_ERROR;
    }

    /* 初始化高速网络设备 */
    ret = engine_init_hs_net_dev(p_g_fr_eng);
    if (ret != FRONTEND_PROXY_PROCESS_OK) {
        printf("[CH1] 错误：高速网络设备初始化失败\n");
        free(p_g_fr_eng->sess_pool);
        return HYPERAMP_ERROR;
    }

    /* 设置队列指针到引擎上下文（不再调用 engine_init_hyperamp_queue，避免重复初始化） */
    p_g_fr_eng->hyper_tx_queue        = ctx->tx_queue;
    p_g_fr_eng->hyper_rx_queue        = ctx->rx_queue;
    p_g_fr_eng->hyper_amp_data_region = ctx->data_region;

    /* 初始化 IoT 设备 */
    ret = engine_init_iot_devices(p_g_fr_eng);
    if (ret != FRONTEND_PROXY_PROCESS_OK) {
        printf("[CH1] 警告：IoT 设备初始化失败，继续运行\n");
    }

    /* 初始化 IoT 会话 */
    ret = engine_init_iot_sessions(p_g_fr_eng);
    if (ret != FRONTEND_PROXY_PROCESS_OK) {
        printf("[CH1] 警告：IoT 会话初始化失败，继续运行\n");
    }

    printf("[CH1] 网络代理子系统初始化完成\n");

    /*
     * 创建双 Session：
     *   Session A (port 8888) — 文本/小文件加密结果
     *   Session B (port 8889) — 图片/大文件 Bulk 加密结果
     */

    /* Session A: 文本端口 */
    printf("[CH1] 正在建立文本 Session (port 8888)...\n");
    struct FrontendSession *text_sess = frontend_sess_new(p_g_fr_eng);
    if (text_sess) {
        frontend_sess_bind_callback(text_sess, ch1_text_session_callback);
        ret = frontend_sess_connect_by_addrstr(text_sess, SESS_UDP_PROTO, "192.168.137.2:8888");
        printf("[CH1] 文本 Session 连接请求已发送 (ret=%d)\n", ret);
        g_ch1_text_session = text_sess;
    } else {
        printf("[CH1] 警告：文本 Session 创建失败\n");
    }

    /* Session B: Bulk/图片端口 */
    printf("[CH1] 正在建立 Bulk Session (port 8889)...\n");
    struct FrontendSession *bulk_sess = frontend_sess_new(p_g_fr_eng);
    if (bulk_sess) {
        frontend_sess_bind_callback(bulk_sess, ch1_bulk_session_callback);
        ret = frontend_sess_connect_by_addrstr(bulk_sess, SESS_UDP_PROTO, "192.168.137.2:8889");
        printf("[CH1] Bulk Session 连接请求已发送 (ret=%d)\n", ret);
        g_ch1_bulk_session = bulk_sess;
    } else {
        printf("[CH1] 警告：Bulk Session 创建失败\n");
    }

    return HYPERAMP_OK;
}

/* ==================== CH1 消息处理 ==================== */

int ch1_process_message(ChannelContext *ctx)
{
    if (!ctx || !ctx->initialized) return HYPERAMP_ERROR;

    /*
     * 直接调用 front 的 run_hyperamp_once()，它会：
     * 1. 从 hyper_rx_queue 出队消息
     * 2. 经过 frontend_proxy_msg_process() 解析代理协议
     * 3. 处理 B2F 活跃队列的回调
     * 4. 处理 F2B 活跃队列的数据发送（包括双 Session 的数据）
     */
    frontend_engine_run_hyperamp_once();

    return HYPERAMP_OK;
}

/* ==================== 就绪状态检查 ==================== */

int ch1_is_text_ready(void)
{
    if (!g_ch1_text_session) return 0;
    return (g_ch1_text_session->sess_state == FRONTEND_SESS_CONNECTED) ? 1 : 0;
}

int ch1_is_bulk_ready(void)
{
    if (!g_ch1_bulk_session) return 0;
    return (g_ch1_bulk_session->sess_state == FRONTEND_SESS_CONNECTED) ? 1 : 0;
}

int ch1_is_ready(void)
{
    return ch1_is_text_ready() || ch1_is_bulk_ready();
}

/* ==================== 内部转发辅助 ==================== */

/**
 * @brief 通过指定 Session 发送 ForwardHeader + 数据
 */
static int ch1_send_with_header(struct FrontendSession *sess,
                                 const uint8_t *data, size_t len,
                                 uint8_t service_id, uint8_t is_bulk,
                                 uint32_t total_length,
                                 const char *label)
{
    if (!sess) {
        printf("[CH0→CH1] %s 转发失败：Session 未建立\n", label);
        return HYPERAMP_ERROR;
    }

    if (sess->sess_state != FRONTEND_SESS_CONNECTED) {
        printf("[CH0→CH1] %s 转发失败：Session 未就绪 (state=%d)\n",
               label, sess->sess_state);
        return HYPERAMP_ERROR;
    }

    g_forward_seq++;

    /* 发送 ForwardHeader */
    ForwardHeader hdr;
    hdr.magic      = FORWARD_HEADER_MAGIC;
    hdr.service_id = service_id;
    hdr.is_bulk    = is_bulk;
    hdr.reserved   = 0;
    hdr.total_len  = total_length;

    int ret = frontend_sess_send(sess, (uint8_t *)&hdr, sizeof(ForwardHeader));
    if (ret < 0) {
        printf("[CH0→CH1] %s 转发失败：ForwardHeader 发送失败 (ret=%d)\n", label, ret);
        return HYPERAMP_ERROR;
    }

    printf("[CH0→CH1] 转发描述头已发送: seq=%u, service=%u, bulk=%u, total_len=%u\n",
           g_forward_seq, service_id, is_bulk, total_length);

    /* 发送实际数据 */
    ret = frontend_sess_send(sess, (uint8_t *)data, (uint32_t)len);
    if (ret >= 0) {
        printf("[CH0→CH1] ✓ %s 数据已转发 (%zu bytes, seq=%u)\n", label, len, g_forward_seq);
        return HYPERAMP_OK;
    }

    printf("[CH0→CH1] ✗ %s 数据转发失败 (ret=%d)\n", label, ret);
    return HYPERAMP_ERROR;
}

/* ==================== 公开转发接口 ==================== */

int ch1_forward_text_data(const uint8_t *data, size_t len, uint8_t service_id)
{
    return ch1_send_with_header(g_ch1_text_session, data, len,
                                service_id, 0, (uint32_t)len, "文本");
}

int ch1_forward_bulk_data(const uint8_t *data, size_t len, uint8_t service_id, uint32_t total_length)
{
    return ch1_send_with_header(g_ch1_bulk_session, data, len,
                                service_id, 1, total_length, "Bulk");
}

int ch1_forward_data(const uint8_t *data, size_t len)
{
    /* 兼容旧接口：走文本 Session */
    return ch1_forward_text_data(data, len, 0);
}

int ch1_forward_bulk_raw_data(const uint8_t *data, size_t len)
{
    if (!g_ch1_bulk_session || g_ch1_bulk_session->sess_state != FRONTEND_SESS_CONNECTED) {
        printf("[CH0→CH1] Bulk 原始数据转发失败：Session 未就绪\n");
        return HYPERAMP_ERROR;
    }
    
    return frontend_sess_send(g_ch1_bulk_session, (uint8_t *)data, (uint32_t)len);
}
