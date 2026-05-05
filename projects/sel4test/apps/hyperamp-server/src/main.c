/*
 * HyperAMP 多通道统一服务器 for seL4
 *
 * 合并 hyperamp-server (CH0) 和 front (CH1) 为单一应用，
 * 实现双通道并发轮询：
 *   - CH0: 图片/文本加解密与验证服务
 *   - CH1: 网络代理前后端通信
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <arch_stdio.h>

#include "channel.h"
#include "channel_ch0.h"
#include "channel_ch1.h"

/* ==================== 全局通道上下文 ==================== */

static ChannelContext g_ch0;  /* CH0: 加解密/验证 */
static ChannelContext g_ch1;  /* CH1: 网络代理 */

/* ==================== 平台初始化辅助 ==================== */

void __plat_putchar(int c);

/* ==================== 主函数 ==================== */

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf("  HyperAMP 多通道统一服务器 for seL4\n");
    printf("  CH0: 加解密/验证  |  CH1: 网络代理\n");
    printf("================================================\n\n");

    /*
     * 从 IPC buffer msg[] 字段读取所有通道的共享内存虚拟地址。
     *
     * 内核 boot.c 布局:
     *   msg[2..4] → CH0 (TX, RX, Data)
     *   msg[5..7] → CH1 (TX, RX, Data)
     *   msg[8..10] → CH2 (TX, RX, Data) - 预留
     *
     * 重要：不要在 seL4_GetMR() 之前插入任何 seL4 系统调用，
     * 否则 IPC buffer 内容会被覆盖！
     */
    seL4_Word ch0_tx = seL4_GetMR(2);
    seL4_Word ch0_rx = seL4_GetMR(3);
    seL4_Word ch0_dt = seL4_GetMR(4);
    seL4_Word ch1_tx = seL4_GetMR(5);
    seL4_Word ch1_rx = seL4_GetMR(6);
    seL4_Word ch1_dt = seL4_GetMR(7);

    printf("[Main] IPC buffer 共享内存地址:\n");
    printf("  CH0: TX=%p, RX=%p, Data=%p\n",
           (void *)ch0_tx, (void *)ch0_rx, (void *)ch0_dt);
    printf("  CH1: TX=%p, RX=%p, Data=%p\n",
           (void *)ch1_tx, (void *)ch1_rx, (void *)ch1_dt);

    /* ==================== 初始化 CH0 ==================== */

    printf("\n[Main] ===== 初始化 CH0 (加解密/验证) =====\n");
    if (channel_init(&g_ch0, CHANNEL_ID_CH0,
                     ch0_tx, ch0_rx, ch0_dt, CH0_QUEUE_CAPACITY) != HYPERAMP_OK) {
        printf("[Main] 致命错误：CH0 初始化失败！\n");
        return -1;
    }

    /* ==================== 初始化 CH1 ==================== */

    printf("\n[Main] ===== 初始化 CH1 (网络代理) =====\n");
    if (ch1_tx && ch1_rx && ch1_dt) {
        if (channel_init(&g_ch1, CHANNEL_ID_CH1,
                         ch1_tx, ch1_rx, ch1_dt, CH1_QUEUE_CAPACITY) != HYPERAMP_OK) {
            printf("[Main] 警告：CH1 通道初始化失败，仅运行 CH0\n");
        } else {
            /* 初始化网络代理子系统 */
            if (ch1_init(&g_ch1) != HYPERAMP_OK) {
                printf("[Main] 警告：CH1 代理子系统初始化失败，仅运行 CH0\n");
                g_ch1.initialized = 0;
            }
        }
    } else {
        printf("[Main] CH1 地址无效，仅运行 CH0 模式\n");
    }

    /* ==================== 主轮询循环 ==================== */

    printf("\n[Main] ===== 进入主轮询循环 =====\n");
    printf("[Main] CH0: %s\n", g_ch0.initialized ? "已就绪" : "未启用");
    printf("[Main] CH1: %s\n", g_ch1.initialized ? "已就绪" : "未启用");
    printf("[Main] ==============================\n\n");

    int idle_count = 0;

    while (1) {
        int had_work = 0;

        /* 轮询 CH0：处理加解密/验证请求 */
        if (g_ch0.initialized) {
            int ret = ch0_process_message(&g_ch0);
            if (ret == HYPERAMP_OK) {
                had_work = 1;
            }
        }

        /* 轮询 CH1：处理网络代理请求 */
        if (g_ch1.initialized) {
            int ret = ch1_process_message(&g_ch1);
            if (ret == HYPERAMP_OK) {
                had_work = 1;
            }
        }

        /* 空闲时轻量延迟，减少 CPU 占用 */
        if (!had_work) {
            idle_count++;
            for (volatile int i = 0; i < 1000; i++);

            /* 定期状态打印（每 100000 次空闲循环） */
            if (idle_count % 100000 == 0) {
                printf("[Main] 轮询中... (idle=%d, CH0=%s, CH1=%s)\n",
                       idle_count,
                       g_ch0.initialized ? "OK" : "OFF",
                       g_ch1.initialized ? "OK" : "OFF");
            }
        } else {
            idle_count = 0;
        }
    }

    /* 永不到达 */
    return 0;
}
