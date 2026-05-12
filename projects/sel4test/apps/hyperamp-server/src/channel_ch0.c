/*
 * CH0 通道处理模块实现 — 图片/文本加解密与验证服务
 *
 * 从原 main.c 提取的 CH0 专用处理逻辑，包括：
 * - Echo / Encrypt / Decrypt 普通消息服务
 * - Bulk Transfer (零拷贝大数据) 处理
 * - 签名验证 + 加解密
 * - 字段验证 + 加解密
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "channel_ch0.h"
#include "channel_ch1.h"
#include "ch0_utils.h"

/* CH0 数据区大小：2MB 通道 - 8KB 队列 */
#define CH0_DATA_SIZE   (2 * 1024 * 1024 - 8192)

/* 为了解决内存耗尽问题，引入 engine 单次运行函数以释放内存池 */
extern void frontend_engine_run_hyperamp_once(void);

/* ==================== 辅助函数 ==================== */

static void ch0_print_hex(const uint8_t *data, size_t len, size_t max_display)
{
    printf("  [HEX] ");
    for (size_t i = 0; i < len && i < max_display; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 16 == 0) printf("\n        ");
    }
    if (len > max_display) printf("... (%zu bytes total)", len);
    printf("\n");
}

static void ch0_print_string(const char *data, size_t len, size_t max_display)
{
    printf("  [STR] \"");
    for (size_t i = 0; i < len && i < max_display; i++) {
        char c = data[i];
        if (c >= 32 && c <= 126) {
            printf("%c", c);
        } else if (c == '\0') {
            break;
        } else {
            printf("\\x%02x", (unsigned char)c);
        }
    }
    if (len > max_display) printf("...");
    printf("\"\n");
}

/* ==================== 回复发送 ==================== */

/**
 * @brief 发送普通回复消息到 Linux (通过 CH0 TX Queue)
 */
static int ch0_send_reply(ChannelContext *ctx, const char *reply_data, size_t reply_len,
                           uint16_t frontend_sess, uint16_t backend_sess)
{
    HyperampMsgHeader msg_hdr = {
        .version = 1,
        .proxy_msg_type = HYPERAMP_MSG_TYPE_DATA,
        .frontend_sess_id = frontend_sess,
        .backend_sess_id = backend_sess,
        .payload_len = (uint16_t)reply_len,
    };

    size_t total_size = sizeof(HyperampMsgHeader) + reply_len;
    if (total_size > DEFAULT_BLOCK_SIZE) {
        printf("[CH0] 回复数据过大: %zu bytes\n", total_size);
        return HYPERAMP_ERROR;
    }

    char msg_buf[4096];
    hyperamp_safe_memcpy(msg_buf, &msg_hdr, sizeof(HyperampMsgHeader));
    if (reply_len > 0) {
        hyperamp_safe_memcpy(msg_buf + sizeof(HyperampMsgHeader), reply_data, reply_len);
    }

    return shm_write_buffer(ctx, msg_buf, total_size);
}

/**
 * @brief 发送 Bulk 回复描述符到 Linux
 */
static int ch0_send_bulk_reply(ChannelContext *ctx, HyperampBulkDescriptor *desc)
{
    HyperampMsgHeader msg_hdr = {
        .version = 1,
        .proxy_msg_type = HYPERAMP_MSG_TYPE_BULK,
        .frontend_sess_id = 0,
        .backend_sess_id = 0,
        .payload_len = sizeof(HyperampBulkDescriptor),
    };

    char msg_buf[128];
    hyperamp_safe_memcpy(msg_buf, &msg_hdr, sizeof(HyperampMsgHeader));
    hyperamp_safe_memcpy(msg_buf + sizeof(HyperampMsgHeader), desc, sizeof(HyperampBulkDescriptor));

    return shm_write_buffer(ctx, msg_buf,
                            sizeof(HyperampMsgHeader) + sizeof(HyperampBulkDescriptor));
}

/* ==================== CH0→CH1 跨通道转发 ==================== */

/**
 * @brief 将普通消息的 CH0 处理结果转发到 CH1 文本端口 (8888)
 *
 * 用于非 Bulk 的加密结果。自动添加 ForwardHeader。
 * 转发失败不影响 CH0 正常应答流程（graceful fallback）。
 */
static int ch0_try_forward_to_ch1(const uint8_t *data, size_t len,
                                    uint8_t service_id)
{
    if (!ch1_is_text_ready()) {
        printf("[CH0→CH1] 文本 Session 未就绪，跳过网络分发\n");
        return HYPERAMP_ERROR;
    }
    return ch1_forward_text_data(data, len, service_id);
}

/**
 * @brief 将 Bulk 共享内存中的处理结果分块转发到 CH1 图片端口 (8889)
 *
 * 先发送 ForwardHeader（通过 ch1_forward_bulk_data 自动完成），
 * 然后分块读取共享内存数据并逐块发送。
 *
 * 注意：ForwardHeader 在第一块之前由 ch1_forward_bulk_data 自动发送，
 * 后续分块直接通过 frontend_sess_send 发送（不重复发 header）。
 */
static int ch0_try_forward_bulk_to_ch1(ChannelContext *ctx,
                                        uint32_t offset, uint32_t length,
                                        uint8_t service_id)
{
    if (!ch1_is_bulk_ready()) {
        printf("[CH0→CH1] Bulk Session 未就绪，跳过 Bulk 数据网络分发\n");
        return HYPERAMP_ERROR;
    }

    volatile uint8_t *src = (volatile uint8_t *)((uintptr_t)ctx->data_region + offset);

    /*
     * 对于 Bulk 数据，先把整块数据拼接后一次性调用 ch1_forward_bulk_data，
     * 让其自动发送 ForwardHeader + 数据。
     * 但如果数据很大，需要分块。这里策略：
     *   第一块：通过 ch1_forward_bulk_data 发送（含 ForwardHeader）
     *   后续块：直接通过底层接口发送（不再发 ForwardHeader）
     */
    #define FORWARD_CHUNK_SIZE 4096
    uint8_t chunk_buf[FORWARD_CHUNK_SIZE];
    uint32_t remaining = length;
    uint32_t pos = 0;
    int first_chunk = 1;

    while (remaining > 0) {
        uint32_t chunk = (remaining > FORWARD_CHUNK_SIZE) ? FORWARD_CHUNK_SIZE : remaining;
        hyperamp_safe_memcpy(chunk_buf, (void *)(src + pos), chunk);

        int ret;
        if (first_chunk) {
            /* 第一块：通过 ch1_forward_bulk_data 发送，它会自动添加 ForwardHeader */
            ret = ch1_forward_bulk_data(chunk_buf, chunk, service_id, length);
            if (ret == HYPERAMP_OK) {
                first_chunk = 0;
                pos += chunk;
                remaining -= chunk;
                /* 发送完第一块后，强制运行一次引擎来释放内存并刷入共享内存 */
                frontend_engine_run_hyperamp_once();
            } else {
                printf("[CH0→CH1] Bulk 第一块转发中断\n");
                return HYPERAMP_ERROR;
            }
        } else {
            /* 后续块：直接发送数据到 bulk 端口，不再加 header */
            ret = ch1_forward_bulk_raw_data(chunk_buf, chunk);
            if (ret < 0) {
                printf("[CH0→CH1] Bulk 分块转发中断，已发送 %u/%u bytes\n", pos, length);
                return HYPERAMP_ERROR;
            }
            if (ret == 0) {
                /* 队列满或内存不足，执行一次引擎运转释放内存，然后重试当前块 */
                frontend_engine_run_hyperamp_once();
                continue; /* 重试这个块 */
            }
            
            /* 累加实际成功放入队列的字节数 */
            pos += ret;
            remaining -= ret;
            
            /* 强制运行一次引擎来释放内存，确保堆内存永远不会耗尽 */
            frontend_engine_run_hyperamp_once();
        }
    }

    printf("[CH0→CH1] ✓ Bulk 数据已转发至图片端口 (port 8889, %u bytes)\n", length);
    return HYPERAMP_OK;
}

/* ==================== 服务处理函数 ==================== */

static int ch0_service_echo(volatile void *data_ptr, size_t length)
{
    printf("[CH0] Echo 服务: %zu bytes\n", length);
    ch0_print_string((const char *)data_ptr, length, 64);
    return HYPERAMP_OK;
}

static int ch0_service_encrypt(ChannelContext *ctx, volatile void *data_ptr, size_t length,
                                uint16_t frontend_sess, uint16_t backend_sess)
{
    printf("[CH0] 加密 %zu bytes\n", length);

    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);

    /* XOR 加密 */
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;
    }

    /* 尝试通过 CH1 文本端口 (8888) 分发加密结果 */
    ch0_try_forward_to_ch1(result_buf, result_len, SERVICE_ENCRYPT);

    /* CH0 仍然返回应答（处理完成确认） */
    int ret = ch0_send_reply(ctx, (const char *)result_buf, result_len,
                              frontend_sess, backend_sess);
    printf("[CH0] %s 加密数据已发送\n", (ret == HYPERAMP_OK) ? "✓" : "✗");
    return ret;
}

static int ch0_service_decrypt(ChannelContext *ctx, volatile void *data_ptr, size_t length,
                                uint16_t frontend_sess, uint16_t backend_sess)
{
    printf("[CH0] 解密 %zu bytes\n", length);

    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);

    /* XOR 解密（对称） */
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;
    }

    /* 尝试通过 CH1 文本端口 (8888) 分发解密结果 */
    ch0_try_forward_to_ch1(result_buf, result_len, SERVICE_DECRYPT);

    /* CH0 仍然返回应答 */
    int ret = ch0_send_reply(ctx, (const char *)result_buf, result_len,
                              frontend_sess, backend_sess);
    printf("[CH0] %s 解密数据已发送\n", (ret == HYPERAMP_OK) ? "✓" : "✗");
    return ret;
}

/* ==================== 签名验证 ==================== */

static int ch0_verify_signed_data(void *data, size_t total_len,
                                   void **payload_out, size_t *payload_len_out)
{
    if (total_len < sizeof(HyperampSignedHeader)) {
        printf("[CH0] 签名验证失败: 数据过短 (%zu bytes)\n", total_len);
        return AUTH_FAILED_BAD_LEN;
    }

    HyperampSignedHeader *hdr = (HyperampSignedHeader *)data;

    if (hdr->magic != SIG_MAGIC) {
        printf("[CH0] 签名验证失败: magic 不匹配 (0x%x)\n", hdr->magic);
        return AUTH_FAILED_BAD_MAGIC;
    }

    if (hdr->sig_len < 64 || hdr->sig_len > 72) {
        printf("[CH0] 签名验证失败: sig_len 异常 (%u)\n", hdr->sig_len);
        return AUTH_FAILED_BAD_SIG;
    }

    size_t expected_total = sizeof(HyperampSignedHeader) + hdr->payload_len;
    if (total_len < expected_total) {
        printf("[CH0] 签名验证失败: 数据截断 (got %zu, need %zu)\n", total_len, expected_total);
        return AUTH_FAILED_BAD_LEN;
    }

    uint8_t *sig = hdr->signature;
    if (sig[0] != 0x30) {
        printf("[CH0] 签名验证失败: 签名格式无效\n");
        return AUTH_FAILED_BAD_SIG;
    }

    *payload_out = (uint8_t *)data + sizeof(HyperampSignedHeader);
    *payload_len_out = hdr->payload_len;

    printf("[CH0] ✓ 签名验证通过 (sig_len=%u, payload_len=%u)\n",
           hdr->sig_len, hdr->payload_len);
    return AUTH_OK;
}

/* ==================== 字段验证 ==================== */

static const char* ch0_simple_strstr(const char *haystack, size_t haystack_len,
                                      const char *needle)
{
    size_t needle_len = 0;
    while (needle[needle_len]) needle_len++;
    if (needle_len > haystack_len) return NULL;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        int match = 1;
        for (size_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) return &haystack[i];
    }
    return NULL;
}

static int ch0_validate_mission_data(const char *data, size_t len)
{
    const char *required_fields[] = {
        "\"ts\":", "\"img\":", "\"lvl\":", "\"scr\":", "\"cnt\":", "\"tgt\":",
    };
    int num_fields = 6;

    printf("[CH0] 验证任务数据字段...\n");
    for (int i = 0; i < num_fields; i++) {
        if (ch0_simple_strstr(data, len, required_fields[i]) == NULL) {
            printf("[CH0] ✗ 缺少必需字段: %s\n", required_fields[i]);
            return VALIDATE_FAILED_MISSING;
        }
    }
    printf("[CH0] ✓ 所有必需字段存在\n");
    return VALIDATE_OK;
}

/* ==================== Bulk 消息处理 ==================== */

static int ch0_process_bulk(ChannelContext *ctx, void *payload_ptr, size_t len)
{
    if (len < sizeof(HyperampBulkDescriptor)) {
        printf("[CH0] 错误：Bulk 消息过短\n");
        return HYPERAMP_ERROR;
    }

    HyperampBulkDescriptor *desc = (HyperampBulkDescriptor *)payload_ptr;
    printf("[CH0] Bulk 处理: offset=0x%x, len=%u, service=%u\n",
           desc->offset, desc->length, desc->service_id);

    if (desc->offset + desc->length > CH0_DATA_SIZE) {
        printf("[CH0] 错误：Bulk 数据越界\n");
        desc->status = -1;
        return ch0_send_bulk_reply(ctx, desc);
    }

    /* 定位共享内存中的数据 */
    volatile uint8_t *data = (volatile uint8_t *)((uintptr_t)ctx->data_region + desc->offset);

    /* 读前 invalidate：Linux uncached 写入 RAM，seL4 必须清除本地缓存 */
    hyperamp_cache_invalidate((volatile void *)data, desc->length);

    switch (desc->service_id) {
        case SERVICE_ENCRYPT:
        case SERVICE_DECRYPT:
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            desc->status = 1;
            break;

        case SERVICE_VERIFY_ENCRYPT:
        case SERVICE_VERIFY_DECRYPT: {
            void *actual_payload = NULL;
            size_t actual_len = 0;
            int result = ch0_verify_signed_data((void *)data, desc->length,
                                                 &actual_payload, &actual_len);
            if (result != AUTH_OK) {
                desc->status = result;
                break;
            }
            volatile uint8_t *payload_data = (volatile uint8_t *)actual_payload;
            for (size_t i = 0; i < actual_len; i++) {
                payload_data[i] ^= 0x5A;
            }
            size_t header_size = sizeof(HyperampSignedHeader);
            desc->offset = desc->offset + header_size;
            desc->length = (uint32_t)actual_len;
            desc->status = 1;
            break;
        }

        case SERVICE_VERIFY_ONLY: {
            void *payload_out;
            size_t payload_len;
            int result = ch0_verify_signed_data((void *)data, desc->length,
                                                 &payload_out, &payload_len);
            desc->status = (result == AUTH_OK) ? 1 : result;
            break;
        }

        case SERVICE_VALIDATE_ENCRYPT: {
            int result = ch0_validate_mission_data((const char *)data, desc->length);
            if (result != VALIDATE_OK) {
                desc->status = result;
                break;
            }
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            desc->status = 1;
            break;
        }

        case SERVICE_VALIDATE_DECRYPT: {
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            int result = ch0_validate_mission_data((const char *)data, desc->length);
            desc->status = (result == VALIDATE_OK) ? 1 : result;
            break;
        }

        default:
            printf("[CH0] 未知 bulk service %u，按加密处理\n", desc->service_id);
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            desc->status = 1;
            break;
    }

    /* 写后 clean：确保处理后的数据刷回物理内存供 Linux 读取 */
    hyperamp_cache_clean((volatile void *)data, desc->length);

    /*
     * 跨通道分发：将安全处理后的 Bulk 数据通过 CH1 网络代理转发。
     * 仅对加密类服务进行转发（解密/验签结果通常仅需返回给请求方）。
     */
    if (desc->status == 1) {
        switch (desc->service_id) {
            case SERVICE_ENCRYPT:
            case SERVICE_VERIFY_ENCRYPT:
            case SERVICE_VALIDATE_ENCRYPT:
            case SERVICE_DECRYPT:
            case SERVICE_VERIFY_DECRYPT:
            case SERVICE_VALIDATE_DECRYPT:
                ch0_try_forward_bulk_to_ch1(ctx, desc->offset, desc->length,
                                              (uint8_t)desc->service_id);
                break;
            default:
                break;
        }
    }

    return ch0_send_bulk_reply(ctx, desc);
}

/* ==================== 普通消息分发 ==================== */

static int ch0_process_normal_message(ChannelContext *ctx,
                                       volatile void *data_ptr, size_t length,
                                       uint16_t service_id,
                                       uint16_t frontend_sess, uint16_t backend_sess)
{
    switch (service_id) {
        case SERVICE_ECHO:
            return ch0_service_echo(data_ptr, length);
        case SERVICE_ENCRYPT:
            return ch0_service_encrypt(ctx, data_ptr, length, frontend_sess, backend_sess);
        case SERVICE_DECRYPT:
            return ch0_service_decrypt(ctx, data_ptr, length, frontend_sess, backend_sess);
        default:
            printf("[CH0] 未知 service %u，执行 echo\n", service_id);
            return ch0_service_echo(data_ptr, length);
    }
}

/* ==================== CH0 消息处理入口 ==================== */

int ch0_process_message(ChannelContext *ctx)
{
    if (!ctx || !ctx->initialized) return HYPERAMP_ERROR;

    char msg_buf[4096];
    size_t msg_len;

    /* 从 RX Queue 出队一条消息 */
    int ret = shm_read_buffer(ctx, msg_buf, sizeof(msg_buf), &msg_len);
    if (ret != HYPERAMP_OK) {
        return ret;  /* HYPERAMP_AGAIN = 队列空 */
    }

    if (msg_len < sizeof(HyperampMsgHeader)) {
        printf("[CH0] 消息过短: %zu bytes\n", msg_len);
        return HYPERAMP_ERROR;
    }

    /* 解析消息头 */
    HyperampMsgHeader *hdr = (HyperampMsgHeader *)msg_buf;
    void *payload_ptr = msg_buf + sizeof(HyperampMsgHeader);
    size_t payload_len = hdr->payload_len;

    printf("[CH0] 收到消息: type=%u, sess=%u/%u, payload=%u bytes\n",
           hdr->proxy_msg_type, hdr->frontend_sess_id, hdr->backend_sess_id,
           hdr->payload_len);

    /* 根据消息类型分发 */
    if (hdr->proxy_msg_type == HYPERAMP_MSG_TYPE_BULK) {
        return ch0_process_bulk(ctx, payload_ptr, payload_len);
    } else if (hdr->proxy_msg_type == HYPERAMP_MSG_TYPE_SERVICE) {
        uint16_t service_id = hdr->frontend_sess_id;
        return ch0_process_normal_message(ctx, payload_ptr, payload_len,
                                           service_id,
                                           hdr->frontend_sess_id, hdr->backend_sess_id);
    } else {
        /* 其他消息类型按 service_id 映射处理 */
        int service_id = hdr->proxy_msg_type + 10;
        return ch0_process_normal_message(ctx, payload_ptr, payload_len,
                                           service_id,
                                           hdr->frontend_sess_id, hdr->backend_sess_id);
    }
}
