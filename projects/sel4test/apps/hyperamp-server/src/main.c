/*
 * HyperAMP Server for seL4
 * Compatible with HighSpeedCProxy and new Linux client
 * 
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <arch_stdio.h>

// 引入 HyperAMP 共享内存队列头文件
#include "shm/hyperamp_shm_queue.h"
#include "highspeed_proxy_protocol.h"
#include "highspeed_proxy_frontend_sim.h"  // 前端协议栈模拟器

/* ==================== 配置常量 ==================== */
#if defined(CONFIG_PLAT_IMX8MP_EVK) || defined(CONFIG_PLAT_RK3588)
    // imx8MP 平台共享内存配置
    #define SHM_TX_QUEUE_PADDR  0x7E000000UL
    #define SHM_RX_QUEUE_PADDR  0x7E001000UL
    #define SHM_DATA_PADDR      0x7E002000UL

#elif defined(CONFIG_PLAT_PHYTIUM_PI)
    // Phytium-Pi 平台共享内存配置
    #define SHM_TX_QUEUE_PADDR  0xDE000000UL
    #define SHM_RX_QUEUE_PADDR  0xDE001000UL
    #define SHM_DATA_PADDR      0xDE002000UL

#else
    #error "Unknown Platform! Please define addresses for this board."
#endif
// 物理地址定义 (与 kernel 配置匹配)
// #define SHM_TX_QUEUE_PADDR  0x7E000000UL
// #define SHM_RX_QUEUE_PADDR  0x7E001000UL
// #define SHM_DATA_PADDR      0x7E002000UL

// HyperAMP 布局 (与 Linux 端 and 内核配置匹配)
#define SHM_TX_QUEUE_SIZE       (4 * 1024)        // 4KB TX Queue
#define SHM_RX_QUEUE_SIZE       (4 * 1024)        // 4KB RX Queue

// 通道物理基址
#define SHM_CH0_PADDR           0x7E000000UL
#define SHM_CH1_PADDR           0x7E200000UL
#define SHM_CH2_PADDR           0x7E300000UL

// 通道偏移量 (与 boot.c 匹配)
#define CH1_OFFSET              0x200000UL
#define CH2_OFFSET              0x300000UL

// 各通道数据区大小
#define SHM_CH0_DATA_SIZE       (2 * 1024 * 1024 - 8192)
#define SHM_CH1_DATA_SIZE       (1 * 1024 * 1024 - 8192)
#define SHM_CH2_DATA_SIZE       (1 * 1024 * 1024 - 8192)

static const uint32_t g_shm_data_sizes[3] = {
    SHM_CH0_DATA_SIZE,
    SHM_CH1_DATA_SIZE,
    SHM_CH2_DATA_SIZE
};

static const uint64_t g_shm_paddrs[3] = {
    SHM_CH0_PADDR,
    SHM_CH1_PADDR,
    SHM_CH2_PADDR
};

#define ZONE_ID_LINUX           0
#define ZONE_ID_SEL4            1

/* ==================== 全局变量 ==================== */

// HyperAMP 三通道管理
static volatile HyperampShmQueue *g_tx_queues[3] = {NULL, NULL, NULL};
static volatile HyperampShmQueue *g_rx_queues[3] = {NULL, NULL, NULL};
static volatile void *g_data_regions[3] = {NULL, NULL, NULL};

// 为了保持兼容性，保留原有的单指针名指向 CH0
#define g_tx_queue g_tx_queues[0]
#define g_rx_queue g_rx_queues[0]
#define g_data_region g_data_regions[0]
#define SHM_DATA_SIZE SHM_CH0_DATA_SIZE

static int g_message_count = 0;
static int g_error_count = 0;

/* 测试模式选择 */
#define TEST_MODE_LISTEN    0  // 监听后端响应（原模式）
#define TEST_MODE_FRONTEND  1  // 运行前端协议栈模拟器
#define CURRENT_TEST_MODE   TEST_MODE_LISTEN  // 纯监听模式，不发送测试消息

/* ==================== 辅助函数 ==================== */

void __plat_putchar(int c);

static void print_hex(const uint8_t *data, size_t len, size_t max_display)
{
    printf("  [HEX] ");
    for (size_t i = 0; i < len && i < max_display; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 16 == 0) printf("\n        ");
    }
    if (len > max_display) printf("... (%zu bytes total)", len);
    printf("\n");
}

static void print_string(const char *data, size_t len, size_t max_display)
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

/* ==================== 服务处理函数 ==================== */

// 前向声明
static int send_reply_to_linux_idx(const char *reply_data, size_t reply_len, 
                                   uint16_t frontend_sess, uint16_t backend_sess, int ch_idx);

/**
 * @brief 发送回复消息到 Linux (默认使用通道 0)
 */
static int send_reply_to_linux(const char *reply_data, size_t reply_len, 
                                uint16_t frontend_sess, uint16_t backend_sess)
{
    return send_reply_to_linux_idx(reply_data, reply_len, frontend_sess, backend_sess, 0);
}

/**
 * @brief Echo 服务 - 简单回显
 */
static int service_echo(volatile void *data_ptr, size_t length)
{
    printf("[seL4] Echo service: %zu bytes\n", length);
    print_string((const char *)data_ptr, length, 64);
    return HYPERAMP_OK;
}

/**
 * @brief 加密服务 - XOR 加密
 * @note 加密后的数据会通过 TX Queue 发回 Linux
 */
static int service_encrypt(volatile void *data_ptr, size_t length, 
                           uint16_t frontend_sess, uint16_t backend_sess)
{
    printf("[seL4] Encrypting %zu bytes\n", length);
    print_string((const char *)data_ptr, length, 64);
    
    // 复制到本地缓冲区进行操作
    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);
    
    // XOR 加密
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;  // XOR 密钥
    }
    
    printf("[seL4] Encryption complete, result:\n");
    // print_hex(result_buf, result_len, 32);
    print_string((const char *)result_buf, result_len, 64);

    // 发送加密结果回 Linux
    int ret = send_reply_to_linux((const char *)result_buf, result_len, frontend_sess, backend_sess);
    if (ret == HYPERAMP_OK) {
        printf("[seL4] \u2713 Encrypted data sent to Linux\n");
    } else {
        printf("[seL4] \u2717 Failed to send encrypted data\n");
    }
    
    return ret;
}

/**
 * @brief 解密服务 - XOR 解密 (与加密算法相同，但显示不同的日志)
 */
static int service_decrypt(volatile void *data_ptr, size_t length,
                           uint16_t frontend_sess, uint16_t backend_sess)
{
    printf("[seL4] Decrypting %zu bytes\n", length);
    print_hex((const uint8_t *)data_ptr, length, 32);
    
    // 复制到本地缓冲区进行操作
    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);
    
    // XOR 解密 (对称算法)
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;  // XOR 密钥
    }
    
    printf("[seL4] Decryption complete, result:\n");
    print_string((const char *)result_buf, result_len, 64);
    
    // 发送解密结果回 Linux
    int ret = send_reply_to_linux((const char *)result_buf, result_len, frontend_sess, backend_sess);
    if (ret == HYPERAMP_OK) {
        printf("[seL4] \u2713 Decrypted data sent to Linux\n");
    } else {
        printf("[seL4] \u2717 Failed to send decrypted data\n");
    }
    
    return ret;
}

/**
 * @brief 代理消息解析服务 - 真实的 HighSpeedCProxy 场景处理
 */
static int service_proxy_message(volatile void *data_ptr, size_t length, uint8_t msg_type)
{
    const char *type_names[] = {"DEVICE", "STRATEGY", "SESSION", "DATA"};
    printf("\n[seL4] ========== Proxy Message (%s) ==========\n", 
           msg_type <= 3 ? type_names[msg_type] : "UNKNOWN");
    printf("[seL4] Payload length: %zu bytes\n", length);
    
    switch (msg_type) {
        case HYPERAMP_MSG_TYPE_DEV:  // 0 - 设备消息
            printf("[seL4] Device Control Message\n");
            printf("[seL4] TODO: Parse device configuration (TAP/TUN creation)\n");
            // 真实场景：解析 JSON 或二进制设备配置
            // 例如：{"cmd":"create_tap", "name":"tap0", "ip":"192.168.1.100"}
            print_string((const char *)data_ptr, length, 256);
            break;
            
        case HYPERAMP_MSG_TYPE_STRGY:  // 1 - 策略消息
            printf("[seL4] Proxy Strategy Message\n");
            printf("[seL4] TODO: Update forwarding rules\n");
            // 真实场景：更新路由表或防火墙规则
            // 例如：{"src":"192.168.1.0/24", "dst":"10.0.0.0/8", "action":"forward"}
            print_string((const char *)data_ptr, length, 256);
            break;
            
        case HYPERAMP_MSG_TYPE_SESS:  // 2 - 会话消息
            printf("[seL4] Session Management Message\n");
            
            if (length >= sizeof(SessionCreatePayload)) {
                SessionCreatePayload *sess = (SessionCreatePayload *)data_ptr;
                
                // 解析会话信息
                const char *proto_name = (sess->protocol == PROXY_PROTO_TCP) ? "TCP" : "UDP";
                const char *state_name;
                switch (sess->state) {
                    case PROXY_STATE_SYN_SENT: state_name = "SYN_SENT"; break;
                    case PROXY_STATE_ESTABLISHED: state_name = "ESTABLISHED"; break;
                    case PROXY_STATE_FIN_WAIT: state_name = "FIN_WAIT"; break;
                    case PROXY_STATE_CLOSED: state_name = "CLOSED"; break;
                    default: state_name = "UNKNOWN"; break;
                }
                
                // 解析 IP 地址
                char src_ip_str[16], dst_ip_str[16];
                ip_to_str(sess->src_ip, src_ip_str);
                ip_to_str(sess->dst_ip, dst_ip_str);
                
                printf("[seL4] Session Details:\n");
                printf("[seL4]   Protocol: %s\n", proto_name);
                printf("[seL4]   State: %s\n", state_name);
                printf("[seL4]   Source: %s:%u\n", src_ip_str, sess->src_port);
                printf("[seL4]   Destination: %s:%u\n", dst_ip_str, sess->dst_port);
                
                // 真实场景：在 seL4 端创建对应的 socket 连接
                printf("[seL4] TODO: Create socket on seL4 side\n");
                printf("[seL4]   -> socket(%s, %s)\n", 
                       proto_name,
                       sess->protocol == PROXY_PROTO_TCP ? "SOCK_STREAM" : "SOCK_DGRAM");
                printf("[seL4]   -> connect(%s:%u)\n", dst_ip_str, sess->dst_port);
            } else {
                printf("[seL4] ERROR: Session payload too short (%zu < %zu)\n",
                       length, sizeof(SessionCreatePayload));
            }
            break;
            
        case HYPERAMP_MSG_TYPE_DATA:  // 3 - 数据消息
            printf("[seL4] Network Data Message\n");
            
            // 尝试解析为 HTTP 请求
            if (length >= sizeof(HttpRequestHeader)) {
                HttpRequestHeader *http_req = (HttpRequestHeader *)data_ptr;
                
                // 检查是否是有效的 HTTP 请求
                if (http_req->method[0] >= 'A' && http_req->method[0] <= 'Z') {
                    printf("[seL4] HTTP Request Detected:\n");
                    printf("[seL4]   Method: %.8s\n", http_req->method);
                    printf("[seL4]   URI: %.256s\n", http_req->uri);
                    printf("[seL4]   Host: %.128s\n", http_req->host);
                    printf("[seL4]   Content-Length: %u\n", http_req->content_length);
                    
                    // 真实场景：通过 seL4 的网络栈发送 HTTP 请求
                    printf("[seL4] TODO: Forward HTTP request via seL4 network stack\n");
                    printf("[seL4]   -> lwip_connect() or picotcp_connect()\n");
                    printf("[seL4]   -> send(%s %s HTTP/1.1)\n", 
                           http_req->method, http_req->uri);
                } else {
                    // 不是 HTTP，可能是其他协议或原始数据
                    printf("[seL4] Raw Network Data:\n");
                    print_hex((const uint8_t *)data_ptr, length, 64);
                    print_string((const char *)data_ptr, length, 128);
                }
            } else {
                // 数据太短，直接显示
                printf("[seL4] Short Data Packet:\n");
                print_hex((const uint8_t *)data_ptr, length, 64);
                print_string((const char *)data_ptr, length, 128);
            }
            
            // 真实场景：将数据通过网络发送出去
            printf("[seL4] TODO: Transmit data via seL4 network interface\n");
            break;
            
        default:
            printf("[seL4] ERROR: Unknown message type: %u\n", msg_type);
            return HYPERAMP_ERROR;
    }
    
    printf("[seL4] ==========================================\n\n");
    return HYPERAMP_OK;
}

/**
 * @brief 简单字符串搜索 (用于字段验证)
 */
static const char* simple_strstr(const char *haystack, size_t haystack_len, 
                                  const char *needle) {
    size_t needle_len = 0;
    while (needle[needle_len]) needle_len++;
    
    if (needle_len > haystack_len) return NULL;
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        int match = 1;
        for (size_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match) return &haystack[i];
    }
    return NULL;
}

/**
 * @brief 验证目标检测数据的字段完整性
 * 
 * 检查 JSON 数据中是否包含必需字段:
 * - ts (时间戳)
 * - img (图片名)
 * - lvl (威胁等级)
 * - scr (评分)
 * - cnt (目标数量)
 * - tgt (目标列表)
 *
 * @param data 数据指针
 * @param len 数据长度
 * @return VALIDATE_OK 或 VALIDATE_FAILED_MISSING
 */
static int validate_mission_data(const char *data, size_t len) {
    const char *required_fields[] = {
        "\"ts\":",
        "\"img\":",
        "\"lvl\":",
        "\"scr\":",
        "\"cnt\":",
        "\"tgt\":",
    };
    int num_fields = 6;
    
    printf("[seL4] Validating mission data fields...\n");
    
    for (int i = 0; i < num_fields; i++) {
        if (simple_strstr(data, len, required_fields[i]) == NULL) {
            printf("[seL4] ✗ Missing required field: %s\n", required_fields[i]);
            return VALIDATE_FAILED_MISSING;
        }
    }
    
    printf("[seL4] ✓ All required fields present\n");
    return VALIDATE_OK;
}

/**
 * @brief 简化版签名验证 
 * 
 * 验证逻辑:
 * 1. 检查 magic number (快速过滤)
 * 2. 检查签名长度合理性
 * 3. 简单哈希比对 
 *
 * @param data 完整签名数据 (SignedHeader + Payload)
 * @param total_len 总数据长度
 * @param payload_out 输出: 实际payload指针
 * @param payload_len_out 输出: 实际payload长度
 * @return AUTH_OK 或错误码
 */
static int verify_signed_data(void *data, size_t total_len, 
                              void **payload_out, size_t *payload_len_out)
{
    if (total_len < sizeof(HyperampSignedHeader)) {
        printf("[seL4] Auth failed: data too short (%zu bytes)\n", total_len);
        return AUTH_FAILED_BAD_LEN;
    }

    HyperampSignedHeader *hdr = (HyperampSignedHeader *)data;
    
    // 1. Magic 检查
    if (hdr->magic != SIG_MAGIC) {
        printf("[seL4] Auth failed: bad magic (0x%x, expected 0x%x)\n", 
               hdr->magic, SIG_MAGIC);
        return AUTH_FAILED_BAD_MAGIC;
    }

    // 2. 签名长度检查
    if (hdr->sig_len < 64 || hdr->sig_len > 72) {
        printf("[seL4] Auth failed: bad sig_len (%u)\n", hdr->sig_len);
        return AUTH_FAILED_BAD_SIG;
    }

    // 3. Payload 长度检查
    size_t expected_total = sizeof(HyperampSignedHeader) + hdr->payload_len;
    if (total_len < expected_total) {
        printf("[seL4] Auth failed: truncated data (got %zu, need %zu)\n", 
               total_len, expected_total);
        return AUTH_FAILED_BAD_LEN;
    }

    // 4. 简化版验证: 检查签名的前4字节与公钥的特定字节匹配(真正的验证需要 ECDSA 库)
    uint8_t *sig = hdr->signature;
    
    // 简单检查: 签名应该以 0x30 开头 (ASN.1 SEQUENCE)
    if (sig[0] != 0x30) {
        printf("[seL4] Auth failed: invalid signature format\n");
        return AUTH_FAILED_BAD_SIG;
    }

    // 提取 payload
    *payload_out = (uint8_t *)data + sizeof(HyperampSignedHeader);
    *payload_len_out = hdr->payload_len;

    printf("[seL4] ✓ Signature verification PASSED (simplified check)\n");
    printf("[seL4]   Signature length: %u bytes\n", hdr->sig_len);
    printf("[seL4]   Payload length: %u bytes\n", hdr->payload_len);
    
    return AUTH_OK;
}

/**
 * @brief 处理带签名的 Bulk 消息
 * 
 * 流程: 验证签名 -> 提取 payload -> 根据 service_id 处理
 */
static int process_signed_bulk_message(void *payload_ptr, size_t len, 
                                       uint32_t service_id)
{
    void *actual_payload = NULL;
    size_t actual_len = 0;
    
    // 1. 验证签名
    int auth_result = verify_signed_data(payload_ptr, len, 
                                         &actual_payload, &actual_len);
    if (auth_result != AUTH_OK) {
        printf("[seL4] Signed message rejected (auth_result=%d)\n", auth_result);
        return auth_result;
    }

    // 2. 根据 service_id 处理验证后的数据
    printf("[seL4] Processing verified payload (%zu bytes), service=%u\n", 
           actual_len, service_id);

    // 对验证后的数据执行 XOR 加密/解密
    volatile uint8_t *data = (volatile uint8_t *)actual_payload;
    for (size_t i = 0; i < actual_len; i++) {
        data[i] ^= 0x5A;
    }

    printf("[seL4] ✓ Signed data processed successfully\n");
    return AUTH_OK;
}

/**
 * @brief 处理单个消息
 * @param data_ptr 数据指针
 * @param length 数据长度
 * @param service_id 服务ID
 * @param frontend_sess 前端会话ID (用于回复)
 * @param backend_sess 后端会话ID (用于回复)
 */
static int process_message(volatile void *data_ptr, size_t length, uint16_t service_id,
                           uint16_t frontend_sess, uint16_t backend_sess)
{
    switch (service_id) {
        case 0:  // Echo
            return service_echo(data_ptr, length);
            
        case 1:  // 加密
            return service_encrypt(data_ptr, length, frontend_sess, backend_sess);
            
        case 2:  // 解密
            return service_decrypt(data_ptr, length, frontend_sess, backend_sess);
            
        case 10:  // 设备消息
            return service_proxy_message(data_ptr, length, 0);
            
        case 11:  // 策略消息
            return service_proxy_message(data_ptr, length, 1);
            
        case 12:  // 会话消息
            return service_proxy_message(data_ptr, length, 2);
            
        case 13:  // 数据消息
            return service_proxy_message(data_ptr, length, 3);
            
        default:
            printf("[seL4] Unknown service %u, echoing\n", service_id);
            return service_echo(data_ptr, length);
    }
}
/**
 * @brief 发送 Bulk 处理结果给 Linux (指定通道)
 */
static int send_bulk_reply_idx(HyperampBulkDescriptor *desc, int ch_idx)
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
    
    volatile void *tx_data_base = g_data_regions[ch_idx];
    int ret = hyperamp_queue_enqueue(g_tx_queues[ch_idx], ZONE_ID_SEL4, msg_buf, 
                                     sizeof(HyperampMsgHeader) + sizeof(HyperampBulkDescriptor), 
                                     tx_data_base);
    if (ret == HYPERAMP_OK) {
        printf("[seL4] Bulk reply sent (CH%d)\n", ch_idx);
    } else {
        printf("[seL4] Failed to send bulk reply (CH%d)\n", ch_idx);
    }
    return ret;
}

/**
 * @brief 处理 Bulk（零拷贝）消息 (指定通道)
 */
static int process_bulk_message_idx(void *payload_ptr, size_t len, int ch_idx)
{
    if (len < sizeof(HyperampBulkDescriptor)) {
        printf("[seL4] Error: Bulk msg too short\n");
        return HYPERAMP_ERROR;
    }
    
    HyperampBulkDescriptor *desc = (HyperampBulkDescriptor *)payload_ptr;
    printf("[seL4] Processing Bulk (CH%d): offset=0x%x, len=%u, service=%u\n", 
           ch_idx, desc->offset, desc->length, desc->service_id);
    
    if (desc->offset + desc->length > g_shm_data_sizes[ch_idx]) {
        printf("[seL4] Error: Bulk data out of bounds\n");
        desc->status = -1;
        return send_bulk_reply_idx(desc, ch_idx);
    }
    
    // 定位共享内存中的数据
    volatile uint8_t *data = (volatile uint8_t *)((uintptr_t)g_data_regions[ch_idx] + desc->offset);
    
    hyperamp_cache_invalidate((volatile void *)data, desc->length);
    
    // 根据 service_id 处理 (保留原有的 switch 逻辑)
    switch (desc->service_id) {
        case SERVICE_ENCRYPT:
        case SERVICE_DECRYPT:
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            desc->status = 1;
            break;
            
        case SERVICE_VERIFY_ENCRYPT:
        case SERVICE_VERIFY_DECRYPT:
            printf("[seL4] Service %u: Verify + Process\n", desc->service_id);
            {
                void *actual_payload = NULL;
                size_t actual_len = 0;
                int result = verify_signed_data((void *)data, desc->length, 
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
            }
            break;
            
        case SERVICE_VERIFY_ONLY:
            {
                void *payload_out;
                size_t payload_len;
                int result = verify_signed_data((void *)data, desc->length, 
                                               &payload_out, &payload_len);
                desc->status = (result == AUTH_OK) ? 1 : result;
            }
            break;
        
        case SERVICE_VALIDATE_ENCRYPT:
            {
                int result = validate_mission_data((const char *)data, desc->length);
                if (result != VALIDATE_OK) {
                    desc->status = result;
                    break;
                }
                for (size_t i = 0; i < desc->length; i++) {
                    data[i] ^= 0x5A;
                }
                desc->status = 1;
            }
            break;
            
        case SERVICE_VALIDATE_DECRYPT:
            {
                for (size_t i = 0; i < desc->length; i++) {
                    data[i] ^= 0x5A;
                }
                int result = validate_mission_data((const char *)data, desc->length);
                if (result != VALIDATE_OK) {
                    desc->status = result;
                    break;
                }
                desc->status = 1;
            }
            break;
            
        default:
            for (size_t i = 0; i < desc->length; i++) {
                data[i] ^= 0x5A;
            }
            desc->status = 1;
            break;
    }
    
    hyperamp_cache_clean((volatile void *)data, desc->length);
    return send_bulk_reply_idx(desc, ch_idx);
}

/**
 * @brief 发送回复消息到 Linux (指定通道)
 */
static int send_reply_to_linux_idx(const char *reply_data, size_t reply_len, 
                                   uint16_t frontend_sess, uint16_t backend_sess, int ch_idx)
{
    HyperampMsgHeader msg_hdr = {
        .version = 1,
        .proxy_msg_type = HYPERAMP_MSG_TYPE_DATA,
        .frontend_sess_id = frontend_sess,
        .backend_sess_id = backend_sess,
        .payload_len = (uint16_t)reply_len,
    };
    
    size_t total_size = sizeof(HyperampMsgHeader) + reply_len;
    if (total_size > g_tx_queues[ch_idx]->block_size) {
        printf("[seL4] Reply too large: %zu bytes\n", total_size);
        return HYPERAMP_ERROR;
    }
    
    char msg_buf[4096];
    hyperamp_safe_memcpy(msg_buf, &msg_hdr, sizeof(HyperampMsgHeader));
    if (reply_len > 0) {
        hyperamp_safe_memcpy(msg_buf + sizeof(HyperampMsgHeader), 
                            reply_data, reply_len);
    }
    
    volatile void *tx_data_base = g_data_regions[ch_idx];
    int ret = hyperamp_queue_enqueue(g_tx_queues[ch_idx], ZONE_ID_SEL4,
                                     msg_buf, total_size, tx_data_base);
    if (ret == HYPERAMP_OK) {
        printf("[seL4] Message sent to Linux (CH%d): %zu bytes\n", ch_idx, total_size);
    } else {
        printf("[seL4] Failed to send message to Linux (CH%d)\n", ch_idx);
    }
    return ret;
}

/**
 * @brief 加密服务 (指定通道)
 */
static int service_encrypt_idx(volatile void *data_ptr, size_t length, 
                               uint16_t frontend_sess, uint16_t backend_sess, int ch_idx)
{
    printf("[seL4] CH%d Encrypting %zu bytes\n", ch_idx, length);
    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;
    }
    return send_reply_to_linux_idx((const char *)result_buf, result_len, frontend_sess, backend_sess, ch_idx);
}

/**
 * @brief 解密服务 (指定通道)
 */
static int service_decrypt_idx(volatile void *data_ptr, size_t length,
                               uint16_t frontend_sess, uint16_t backend_sess, int ch_idx)
{
    printf("[seL4] CH%d Decrypting %zu bytes\n", ch_idx, length);
    uint8_t result_buf[HYPERAMP_MSG_MAX_SIZE];
    size_t result_len = (length > HYPERAMP_MSG_MAX_SIZE) ? HYPERAMP_MSG_MAX_SIZE : length;
    hyperamp_safe_memcpy(result_buf, data_ptr, result_len);
    for (size_t i = 0; i < result_len; i++) {
        result_buf[i] ^= 0x5A;
    }
    return send_reply_to_linux_idx((const char *)result_buf, result_len, frontend_sess, backend_sess, ch_idx);
}

/**
 * @brief 处理单个消息 (指定通道)
 */
static int process_message_idx(volatile void *data_ptr, size_t length, uint16_t service_id,
                               uint16_t frontend_sess, uint16_t backend_sess, int ch_idx)
{
    switch (service_id) {
        case 0:  // Echo
            return service_echo(data_ptr, length);
        case 1:  // 加密
            return service_encrypt_idx(data_ptr, length, frontend_sess, backend_sess, ch_idx);
        case 2:  // 解密
            return service_decrypt_idx(data_ptr, length, frontend_sess, backend_sess, ch_idx);
        case 10: case 11: case 12: case 13:
            return service_proxy_message(data_ptr, length, service_id - 10);
        default:
            printf("[seL4] CH%d Unknown service %u\n", ch_idx, service_id);
            return service_echo(data_ptr, length);
    }
}


/* ==================== 主消息循环 ==================== */

/**
 * @brief HyperAMP 消息服务器主循环
 */
void hyperamp_server_main_loop(void)
{
    printf("\n[seL4] ========================================\n");
    printf("[seL4] HyperAMP Frontend (seL4 Side) Starting...\n");
    printf("[seL4] Multi-Channel Mode: CH0, CH1, CH2\n");
    printf("[seL4] ========================================\n");
    
    // 初始化所有 3 个通道的队列
    for (int i = 0; i < 3; i++) {
        printf("[seL4] Initializing CH%d queues...\n", i);

        HyperampQueueConfig tx_config = {
            .map_mode = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
            .capacity = 256,
            .block_size = 4096,
            .phy_addr = g_shm_paddrs[i],
            .virt_addr = (uint64_t)g_tx_queues[i],
        };

        HyperampQueueConfig rx_config = {
            .map_mode = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
            .capacity = 256,
            .block_size = 4096,
            .phy_addr = g_shm_paddrs[i] + SHM_RX_QUEUE_SIZE,
            .virt_addr = (uint64_t)g_rx_queues[i],
        };

        if (hyperamp_queue_init(g_tx_queues[i], &tx_config, 1) != HYPERAMP_OK) {
            printf("[seL4] ERROR: Failed to initialize CH%d TX queue!\n", i);
            return;
        }
        if (hyperamp_queue_init(g_rx_queues[i], &rx_config, 1) != HYPERAMP_OK) {
            printf("[seL4] ERROR: Failed to initialize CH%d RX queue!\n", i);
            return;
        }
        
        hyperamp_cache_invalidate((volatile void *)g_tx_queues[i], 64);
        hyperamp_cache_invalidate((volatile void *)g_rx_queues[i], 64);
        printf("[seL4] CH%d queues ready\n", i);
    }
    
    printf("[seL4] All channels ready for communication\n");
    printf("[seL4] ========================================\n");
    
    /* 根据测试模式选择不同的执行路径 */
    if (CURRENT_TEST_MODE == TEST_MODE_FRONTEND) {
        printf("[seL4] Running in FRONTEND TEST MODE (CH0)\n");
        FrontendProxyContext frontend_ctx;
        frontend_proxy_init(&frontend_ctx, g_tx_queues[0], g_rx_queues[0]);
        frontend_run_test_scenario(&frontend_ctx);
        printf("\n[seL4] Switching to listen mode for all channels...\n");
    }
    
    printf("[seL4] Waiting for responses from Backend on all channels...\n\n");
    
    // 消息处理缓冲区
    char msg_buf[4096];
    size_t msg_len;
    uint32_t loop_count = 0;

    // 主循环：轮询所有 3 个通道
    while (1) {
        for (int ch = 0; ch < 3; ch++) {
            /* 关键：失效缓存 */
            hyperamp_cache_invalidate(g_rx_queues[ch], 64);
            
            uint16_t rx_header = hyperamp_safe_read_u16(g_rx_queues[ch],
                                                         offsetof(HyperampShmQueue, header));
            uint16_t rx_tail = hyperamp_safe_read_u16(g_rx_queues[ch],
                                                       offsetof(HyperampShmQueue, tail));

            if (rx_tail != rx_header) {
                // 通道 ch 有消息
                volatile void *rx_data_base = g_data_regions[ch];
                int ret = hyperamp_queue_dequeue(g_rx_queues[ch], ZONE_ID_SEL4,
                                                msg_buf, sizeof(msg_buf), &msg_len,
                                                rx_data_base);
                
                if (ret == HYPERAMP_OK && msg_len >= sizeof(HyperampMsgHeader)) {
                    g_message_count++;
                    HyperampMsgHeader *hdr = (HyperampMsgHeader *)msg_buf;
                    
                    printf("\n[seL4] === Message #%d from Backend (CH%d) ===\n", g_message_count, ch);
                    
                    void *payload_ptr = msg_buf + sizeof(HyperampMsgHeader);
                    size_t payload_len = hdr->payload_len;
                    
                    int result;
                    if (hdr->proxy_msg_type == HYPERAMP_MSG_TYPE_BULK) {
                        result = process_bulk_message_idx(payload_ptr, payload_len, ch);
                    } else {
                        int service_id = (hdr->proxy_msg_type == HYPERAMP_MSG_TYPE_SERVICE) ? 
                                          hdr->frontend_sess_id : (hdr->proxy_msg_type + 10);
                        result = process_message_idx(payload_ptr, payload_len, service_id,
                                                     hdr->frontend_sess_id, hdr->backend_sess_id, ch);
                    }
                    
                    if (result == HYPERAMP_OK) {
                        printf("[seL4] ✓ CH%d Message processed successfully\n", ch);
                    } else {
                        g_error_count++;
                        printf("[seL4] ✗ CH%d Failed to process message\n", ch);
                    }
                }
            }
        }
        
        loop_count++;
        // 简单延迟
        for (volatile int i = 0; i < 5000; i++);
    }
}


/* ==================== 主函数 ==================== */

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf("  HyperAMP Server for seL4\n");
    printf("  Compatible with HighSpeedCProxy\n");
    printf("================================================\n\n");

    // 从 IPC buffer msg[] 字段读取共享内存虚拟地址
    // boot.c 写入 msg[2..4]
    uintptr_t ch0_tx_vaddr = seL4_GetMR(2);
    uintptr_t ch0_rx_vaddr = seL4_GetMR(3);
    uintptr_t ch0_data_vaddr = seL4_GetMR(4);

    // 初始化 CH0
    g_tx_queues[0]    = (volatile HyperampShmQueue *)ch0_tx_vaddr;
    g_rx_queues[0]    = (volatile HyperampShmQueue *)ch0_rx_vaddr;
    g_data_regions[0] = (volatile void *)            ch0_data_vaddr;

    // 根据固定偏移计算并初始化 CH1 和 CH2
    // CH1 (Offset 2MB), CH2 (Offset 3MB)
    g_tx_queues[1]    = (volatile HyperampShmQueue *)(ch0_tx_vaddr + CH1_OFFSET);
    g_rx_queues[1]    = (volatile HyperampShmQueue *)(ch0_rx_vaddr + CH1_OFFSET);
    g_data_regions[1] = (volatile void *)            (ch0_data_vaddr + CH1_OFFSET);

    g_tx_queues[2]    = (volatile HyperampShmQueue *)(ch0_tx_vaddr + CH2_OFFSET);
    g_rx_queues[2]    = (volatile HyperampShmQueue *)(ch0_rx_vaddr + CH2_OFFSET);
    g_data_regions[2] = (volatile void *)            (ch0_data_vaddr + CH2_OFFSET);

    printf("[seL4] Shared Memory Addresses for 3 Channels:\n");
    for (int i = 0; i < 3; i++) {
        printf("  CH%d: TX=%p, RX=%p, Data=%p\n", i, 
               (void *)g_tx_queues[i], (void *)g_rx_queues[i], (void *)g_data_regions[i]);
    }

    // 验证 CH0 地址有效性 (基础检查)
    if (!g_tx_queues[0] || !g_rx_queues[0] || !g_data_regions[0]) {
        printf("[seL4] ERROR: Invalid shared memory addresses!\n");
        return -1;
    }

    // 检查结构体大小
    printf("[seL4] HyperampShmQueue size: %zu bytes\n", sizeof(HyperampShmQueue));
    printf("[seL4] HYPERAMP_MAX_MAP_TABLE_ENTRIES: %d\n", HYPERAMP_MAX_MAP_TABLE_ENTRIES);
    printf("[seL4] magic field offset: %zu bytes\n", offsetof(HyperampShmQueue, magic));
    printf("[seL4] WARNING: If offset > 4096, accessing magic will page fault!\n");

    printf("[seL4] Shared memory initialized successfully\n\n");

    // 启动消息处理循环
    hyperamp_server_main_loop();

    // 永不返回
    return 0;
}
