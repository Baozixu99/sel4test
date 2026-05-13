/*
 * CH2 远程内存访问通道 — 实现
 *
 * 封装 monkey-mnemosyne 的 C facade API，提供与 CH0/CH1 一致的
 * 初始化和轮询接口。
 *
 * monkey-mnemosyne 自身是 C++ 实现，但通过 mnemosyne_api.h 暴露
 * 纯 C 接口（extern "C"），因此本文件可以作为普通 C 源文件编译。
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include "channel_ch2.h"
#include <mnemosyne_api.h>

/* ==================== CH2 配置常量 ==================== */

/*
 * mnemosyne 服务器配置。
 * 当 CH2 作为 hyperamp-server 的一部分运行时，
 * server_ip 为空字符串表示稍后由 Session 连接时指定。
 */
#define CH2_MNEMOSYNE_SERVER_IP     "192.168.137.2"
#define CH2_MNEMOSYNE_SERVER_PORT   10100
#define CH2_MNEMOSYNE_AUTH_KEY      "f578bd06-6f8e-42b3-8be9-860c7c645549"

/* ==================== CH2 初始化 ==================== */

int ch2_init(ChannelContext *ctx,
             seL4_Word tx_va, seL4_Word rx_va, seL4_Word data_va)
{
    if (!ctx || !ctx->initialized) {
        printf("[CH2] 错误：通道上下文未初始化\n");
        return HYPERAMP_ERROR;
    }

    printf("[CH2] 正在初始化远程内存访问子系统...\n");
    printf("[CH2]   TX=%p, RX=%p, Data=%p\n",
           (void *)tx_va, (void *)rx_va, (void *)data_va);

    /*
     * 使用方案 A 入口：传入外部已读取的 vaddr，
     * 避免 mnemosyne 内部再次调用 seL4_GetMR() 读到已被覆盖的 IPC buffer。
     */
    int ret = mnemosyne_engine_init_with_vaddrs(
        CH2_MNEMOSYNE_SERVER_IP,
        CH2_MNEMOSYNE_SERVER_PORT,
        CH2_MNEMOSYNE_AUTH_KEY,
        MNEMOSYNE_CHANNEL_DEFAULT,  /* channel_id = 2 */
        MNEMOSYNE_ROLE_ALLOC,       /* seL4 端作为 alloc 角色 */
        (uint64_t)tx_va,
        (uint64_t)rx_va,
        (uint64_t)data_va
    );

    if (ret != 0) {
        printf("[CH2] mnemosyne_engine_init_with_vaddrs 失败, ret=%d\n", ret);
        return HYPERAMP_ERROR;
    }

    printf("[CH2] ✓ 远程内存访问子系统初始化成功\n");
    return HYPERAMP_OK;
}

/* ==================== CH2 轮询处理 ==================== */

int ch2_process_message(ChannelContext *ctx)
{
    if (!ctx || !ctx->initialized) {
        return HYPERAMP_ERROR;
    }

    /*
     * monkey-mnemosyne 的通信由 Session 内部自驱动
     * （send/recv 直接操作 HyperAMP queue），
     * 不需要像 CH1 的 frontend_engine 那样有独立的事件循环。
     *
     * 这里调用 mnemosyne_engine_run_hyperamp_once() 是为了保持
     * 与 CH0/CH1 主循环结构的一致性。当前该函数为 no-op。
     */
    mnemosyne_engine_run_hyperamp_once();

    return HYPERAMP_AGAIN;  /* 无显式工作产出 */
}
