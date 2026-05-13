/**
 * @file hyperamp_protocol_defs.h
 * @brief HyperAMP proxy protocol definitions – monkey-mnemosyne subset
 *
 * Extracted from apps/front/include/message.h.  Contains only the structures
 * and enumerations required by HyperAmpBridge:
 *
 *   - SessMsgHeader      (session create/close sub-header)
 *   - SessIPv4Params     (IPv4 session parameters)
 *   - SessOpRespData     (session operation response)
 *   - Related enums      (SessMsgType, SessIpProtoVersion, SessTranProto,
 *                          SessOpStatus, ActionType)
 *   - PROXY_PROTO_VERSION_1 / PROXY_PROTO_SESS_VERSION_1
 *
 * This file has NO dependency on common_utils.h, message.h, or any other
 * apps/front header.  All definitions are self-contained.
 */

#ifndef HYPERAMP_PROTOCOL_DEFS_LOCAL_H
#define HYPERAMP_PROTOCOL_DEFS_LOCAL_H

#include <stdint.h>

/* ==================== Protocol version constants ==================== */

#define PROXY_PROTO_VERSION_1          1
#define PROXY_PROTO_SESS_VERSION_1     1

/* ==================== Action type ==================== */

typedef enum {
    ACTION_TYPE_COMMAND  = 0,
    ACTION_TYPE_RESPONSE = 1
} ActionType;

/* ==================== Session message header ==================== */

typedef struct {
    uint16_t version;
    uint16_t msg_type;
    uint16_t action_type;
    uint16_t ip_version;
    uint16_t payload_len;
} __attribute__((packed)) SessMsgHeader;

typedef enum {
    SESS_MSG_CLOSE  = 0,
    SESS_MSG_CREATE = 1
} SessMsgType;

typedef enum {
    SESS_NON_IP_PROTO = 0,
    SESS_IPV4_PROTO   = 4,
    SESS_IPV6_PROTO   = 6
} SessIpProtoVersion;

typedef enum {
    SESS_UDP_PROTO      = 0,
    SESS_TCP_PROTO      = 1,
    SESS_FASTPATH_PROTO = 2
} SessTranProto;

/* ==================== IPv4 address / endpoint ==================== */

struct IPv4Address {
    uint8_t data[4];
} __attribute__((packed));

typedef struct {
    struct IPv4Address ipv4_addr;
    uint16_t           port;
} __attribute__((packed)) IPv4PortTuple;

/* ==================== Session IPv4 parameters ==================== */

typedef struct {
    uint16_t       device_selection;
    uint16_t       transport_layer_proto;
    IPv4PortTuple  dest_endpoint;
} __attribute__((packed)) SessIPv4Params;

/* ==================== Session operation response ==================== */

typedef enum {
    SESS_OP_STATUS_SUCCESS = 0,
    SESS_OP_STATUS_FAIL    = 1
} SessOpStatus;

typedef struct SessOpRespData_ {
    uint8_t status;
    uint8_t code;
} __attribute__((packed)) SessOpRespData;


#endif /* HYPERAMP_PROTOCOL_DEFS_LOCAL_H */
