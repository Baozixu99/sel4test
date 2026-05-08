/*
 * CH1 通道处理模块实现 — 网络代理
 *
 * 通过设置 front 引擎的全局变量并调用 front 的初始化和轮询函数，
 * 实现 CH1 网络代理处理。避免重复实现 front 的复杂协议栈逻辑。
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

/* CH1 活跃 Session 句柄（供跨通道转发使用） */
static struct FrontendSession *g_ch1_active_session = NULL;

/**
 * @brief CH1 专用的 Session 事件回调
 *
 * 替换 default_session_event_callback，后者在 CONN 事件时
 * 会自动发送 "test msg"，可能因目标无监听而触发 ICMP 错误，
 * 导致后端关闭 Session。此回调仅记录日志，不主动发送数据。
 */
static void ch1_session_event_callback(struct FrontendSession *sess,
                                        FrontendSessionEvent event)
{
    switch (event) {
        case FRONTEND_SESS_EVENT_CONN:
            printf("[CH1] Session 已连接，跨通道转发就绪\n");
            break;
        case FRONTEND_SESS_EVENT_RECVDATA:
            printf("[CH1] 收到后端数据\n");
            break;
        case FRONTEND_SESS_EVENT_CLOSE:
            printf("[CH1] Session 已关闭\n");
            g_ch1_active_session = NULL;
            break;
        case FRONTEND_SESS_EVENT_ABNORMAL:
            printf("[CH1] Session 异常\n");
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
     * 建立网络代理 Session，用于：
     * 1. CH0 安全处理后结果的跨通道分发
     * 2. 验证前后端通信链路
     */
    printf("[CH1] 正在建立网络代理 Session...\n");
    struct FrontendSession *sess = frontend_sess_new(p_g_fr_eng);
    if (sess) {
        /*
         * 绑定自定义回调，替换默认的 default_session_event_callback。
         * 默认回调在 CONN 事件时会自动发送 "test msg"，如果目标端口
         * 没有监听程序，会触发 ICMP Port Unreachable → epoll error →
         * 后端关闭 Session → 后续跨通道转发的数据无法送达。
         */
        frontend_sess_bind_callback(sess, ch1_session_event_callback);

        ret = frontend_sess_connect_by_addrstr(sess, SESS_UDP_PROTO, "192.168.137.2:8888");
        printf("[CH1] Session 连接请求已发送 (ret=%d)\n", ret);
        /* 保存 Session 句柄，供 CH0 跨通道转发使用 */
        g_ch1_active_session = sess;
    } else {
        printf("[CH1] 警告：Session 创建失败，跨通道转发将不可用\n");
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
     * 4. 处理 F2B 活跃队列的数据发送
     */
    frontend_engine_run_hyperamp_once();

    return HYPERAMP_OK;
}

/* ==================== CH1 跨通道转发接口 ==================== */

int ch1_is_ready(void)
{
    if (!g_ch1_active_session) return 0;
    /* Session 状态 FRONTEND_SESS_CONNECTED = 2 */
    return (g_ch1_active_session->sess_state == FRONTEND_SESS_CONNECTED) ? 1 : 0;
}

int ch1_forward_data(const uint8_t *data, size_t len)
{
    if (!g_ch1_active_session) {
        printf("[CH0→CH1] 转发失败：Session 未建立\n");
        return HYPERAMP_ERROR;
    }

    if (g_ch1_active_session->sess_state != FRONTEND_SESS_CONNECTED) {
        printf("[CH0→CH1] 转发失败：Session 未就绪 (state=%d)\n",
               g_ch1_active_session->sess_state);
        return HYPERAMP_ERROR;
    }

    int ret = frontend_sess_send(g_ch1_active_session, (uint8_t *)data, (uint32_t)len);
    if (ret >= 0) {
        printf("[CH0→CH1] ✓ 安全处理结果已提交网络代理分发 (%zu bytes)\n", len);
        return HYPERAMP_OK;
    }

    printf("[CH0→CH1] ✗ 网络代理分发失败 (ret=%d)\n", ret);
    return HYPERAMP_ERROR;
}
