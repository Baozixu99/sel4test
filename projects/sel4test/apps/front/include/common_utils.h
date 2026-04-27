#ifndef COMMON_UTILS_H_
#define COMMON_UTILS_H_

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "message.h"

#define UTILS_ENABLE_DEBUG                        1

void error_print(char *error);

int debug_print(const char *format, ...);

#if UTILS_ENABLE_DEBUG
#define utils_print(...) printf(__VA_ARGS__)
#else
#define utils_print(...) (void)(0)
#endif


/**
 * @brief Print the content of a uint8_t array in the specified format
 * @param BUF Pointer to the start of the uint8_t array (must be a valid pointer)
 * @param SIZE Number of elements in the array (must be a non-negative integer, preferably of type size_t)
 * @param FORMAT printf-style format string (must match the uint8_t type, e.g., "%02hhx ", "%hhu ", "%hho ", "%c")
 * 
 * Notes:
 * 1. The format specifier must use the "hh" length modifier for uint8_t (to avoid sign extension issues):
 *    - Decimal: %hhu (unsigned)
 *    - Hexadecimal: %hhx (lowercase), %hhX (uppercase)
 *    - Octal: %hho
 *    - ASCII: %c (Note: Non-printable characters may display abnormally; handle them as needed)
 * 2. Iterates from index 0 to SIZE-1, outputting each element in the specified FORMAT
 * 3. Automatically adds a newline after output to separate different buffer contents
 */
#define DUMP_BUFFER_CONTENT(BUF, SIZE, FORMAT) do { \
    /* Boundary check: Return directly if SIZE is 0 to avoid invalid loops */ \
    if ((SIZE) == 0) { \
        printf("Buffer is empty (size = 0)\n"); \
        break; \
    } \
    /* Traverse the array and output each element in the specified format */ \
    for (size_t i = 0; i < (SIZE); ++i) { \
        /* Force cast to uint8_t* to ensure type correctness and avoid pointer type mismatch */ \
        printf(FORMAT, ((const uint8_t*)(BUF))[i]); \
    } \
    /* Add a newline after output to distinguish between different buffer outputs */ \
    printf("\n"); \
} while (0)


/**
 * @def DUMP_PROXY_MSG_HEADER(BUF_PTR)
 * @brief Parse and print ProxyMsgHeader from raw uint8_t buffer
 * 
 * This macro converts a uint8_t* raw memory pointer to ProxyMsgHeader structure,
 * then prints all fields with human-readable format (decimal + hexadecimal),
 * including semantic explanation for message type and range check for payload length.
 * 
 * @param BUF_PTR Input buffer pointer (uint8_t*), points to the start of ProxyMsgHeader in raw memory
 * 
 * @note 1. The buffer must have at least sizeof(ProxyMsgHeader) valid bytes
 * @note 2. uint16_t fields are converted from network byte order to host byte order via ntohs()
 * @note 3. Null pointer check is included to avoid segmentation fault
 * @warning The input buffer must use packed memory layout (match __attribute__((packed)))
 */
#define DUMP_PROXY_MSG_HEADER(BUF_PTR) do { \
    /* 1. Null pointer check to prevent segmentation fault */ \
    if ((BUF_PTR) == NULL) { \
        printf("[ProxyMsgHeader] Error: Input buffer pointer is NULL!\n"); \
        break; \
    } \
    /* 2. Cast uint8_t* to ProxyMsgHeader* to parse byte stream as structured data */ \
    const ProxyMsgHeader* header = (const ProxyMsgHeader*)(BUF_PTR); \
    /* 3. Print all header fields (no endianness conversion required) */ \
    printf("============= ProxyMsgHeader =============\n"); \
    printf("version:          %u (0x%02X)\n", header->version, header->version); \
    /* Print proxy message type with semantic explanation for readability */ \
    printf("proxy_msg_type:   %u (0x%02X) -> ", header->proxy_msg_type, header->proxy_msg_type); \
    switch (header->proxy_msg_type) { \
        case 0: printf("device message\n"); break; \
        case 1: printf("strategy message\n"); break; \
        case 2: printf("session message\n"); break; \
        case 3: printf("data message\n"); break; \
        default: printf("unknown message type\n"); break; \
    } \
    /* Directly print uint16_t fields (no ntohs() for endianness conversion) */ \
    printf("frontend_sess_id: %u (0x%04X)\n", header->frontend_sess_id, header->frontend_sess_id); \
    printf("backend_sess_id:  %u (0x%04X)\n", header->backend_sess_id, header->backend_sess_id); \
    printf("payload_len:      %u (0x%04X) bytes\n", header->payload_len, header->payload_len); \
    /* Validate payload length against valid range (1~4088) */ \
    uint16_t payload_len = header->payload_len; \
    if (payload_len < 1 || payload_len > 4088) { \
        printf("⚠️  Warning: payload_len (%u) is out of valid range (1~4088)!\n", payload_len); \
    } \
    printf("===========================================\n"); \
} while (0)

int parse_proxy_protocol_and_print(const uint8_t *buffer);

struct GeneralProxyMsgHeader_;
typedef struct GeneralProxyMsgHeader_ GeneralProxyMsgHeader;
void print_general_proxy_msg_header(const GeneralProxyMsgHeader *hdr);

struct IotMsgBuffer_;
typedef struct IotMsgBuffer_ IotMsgBuffer;
void print_iot_msg_buffer(const IotMsgBuffer *buf);

#endif