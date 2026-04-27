#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "common_utils.h"

#define PROXY_PROTO_VERSION_1                            1
// #define PROXY_MSG_TYPE_DEV                               0
// #define PROXY_MSG_TYPE_STRGY                             1
// #define PROXY_MSG_TYPE_SESS                              2
// #define PROXY_MSG_TYPE_DATA                              3


typedef enum {
    PROXY_MSG_TYPE_DEV = 0,    // Device message
    PROXY_MSG_TYPE_STRGY,      // Strategy message
    PROXY_MSG_TYPE_SESS,       // Session message
    PROXY_MSG_TYPE_DATA,       // Data message
    PROXY_MSG_TYPE_IOT         // IoT message
} ProxyMsgType;


#define PROXY_PROTO_DEV_VERSION_1                        1
#define PROXY_PROTO_STRGY_VERSION_1                      1
#define PROXY_PROTO_SESS_VERSION_1                       1



#define PROXY_MSG_HDR_SIZE                               8
#define PROXY_MSG_MIN_SIZE                               1
#define PROXY_MSG_MAX_SIZE                               4088
// Sum of header size and maximum message size (total maximum size including header, in bytes)
#define PROXY_MSG_HDR_PLUS_MAX_SIZE                      (PROXY_MSG_HDR_SIZE + PROXY_MSG_MAX_SIZE) 
#define PROXY_MSG_INVALID_LEN                            -1

struct IPv4Address {
    uint8_t data[4];  
}__attribute__((packed));

struct IPv6Address {
    uint8_t data[16]; 
};

union IPAddress {
    struct IPv4Address ipv4_addr;
    struct IPv6Address ipv6_addr; 
}__attribute__((packed));


/*
 * Macro: Copy IPv4 address from struct in_addr to custom struct IPv4Address
 * Parameters:
 *   dest - Destination structure pointer (struct IPv4Address*)
 *   src  - Source structure pointer (const struct in_addr*)
 * Notes:
 *   1. Converts 32-bit network byte order address to 4-byte array in host order
 *   2. Includes null pointer check to prevent invalid memory access
 */
#define COPY_IN_TO_IPV4(dest, src) do { \
    if ((dest) != NULL && (src) != NULL) { \
        uint32_t addr = ntohl((src)->s_addr);  \
        (dest)->data[0] = (addr >> 24) & 0xFF; \
        (dest)->data[1] = (addr >> 16) & 0xFF; \
        (dest)->data[2] = (addr >> 8) & 0xFF; \
        (dest)->data[3] = addr & 0xFF; \
    } \
} while (0)

/*
 * Macro: Copy data from custom struct IPv4Address to struct in_addr
 * Parameters:
 *   dest - Destination structure pointer (struct in_addr*)
 *   src  - Source structure pointer (const struct IPv4Address*)
 * Notes:
 *   1. Combines 4-byte array into 32-bit value in network byte order
 *   2. Reverse operation of COPY_IN_TO_IPV4 macro
 */
#define COPY_IPV4_TO_IN(dest, src) do { \
    if ((dest) != NULL && (src) != NULL) { \
        uint32_t addr = ((uint32_t)(src)->data[0] << 24) | \
                       ((uint32_t)(src)->data[1] << 16) | \
                       ((uint32_t)(src)->data[2] << 8) | \
                       (uint32_t)(src)->data[3]; \
        (dest)->s_addr = htonl(addr);  \
    } \
} while (0)


/*
 * Macro: Copy IPv6 address from struct in6_addr to custom struct IPv6Address
 * Parameters:
 *   dest - Destination structure pointer (struct IPv6Address*)
 *   src  - Source structure pointer (const struct in6_addr*)
 * Notes:
 *   1. Internally uses memcpy to copy 16 bytes of data (their memory layouts are compatible)
 *   2. Includes null pointer check to avoid accessing null pointers
 */
#define COPY_IN6_TO_IPV6(dest, src) do { \
    if ((dest) != NULL && (src) != NULL) { \
        memcpy((dest)->data, (src)->s6_addr, 16); \
    } \
} while (0)

/*
 * Macro: Copy data from custom struct IPv6Address to struct in6_addr
 * Parameters:
 *   dest - Destination structure pointer (struct in6_addr*)
 *   src  - Source structure pointer (const struct IPv6Address*)
 * Notes:
 *   Reverse copy, functionally symmetric to the above macro
 */
#define COPY_IPV6_TO_IN6(dest, src) do { \
    if ((dest) != NULL && (src) != NULL) { \
        memcpy((dest)->s6_addr, (src)->data, 16); \
    } \
} while (0)


/**
 * @brief Proxy message header structure
 * 
 * This structure defines the header format for proxy messages, containing metadata 
 * such as protocol version, message type, session identifiers, and payload length.
 */
typedef struct {
    uint8_t     version;             // Protocol version. Currently unused, set to 1.
    uint8_t     proxy_msg_type;      // Proxy message type. Possible values: device message (0), strategy message (1), session message (2), data message (3).
    uint16_t    frontend_sess_id;    // Frontend session ID. Used for matching frontend and backend sessions in the frontend proxy.
    uint16_t    backend_sess_id;     // Backend session ID. Used for matching frontend and backend sessions in the backend proxy.
    uint16_t    payload_len;         // Payload length in bytes. Valid range: 1 to 4088. Must not exceed one physical page.
} __attribute__((packed)) ProxyMsgHeader;




/**
 * Calculate the total memory space occupied by the complete message described by ProxyMsgHeader
 * Including: size of the header structure itself + length of the payload data
 */
#define PROXY_MSG_TOTAL_SIZE(p_msg_header) \
    (sizeof(ProxyMsgHeader) + (p_msg_header)->payload_len)


/**
 * @brief Macro to calculate total shared queue memory size with fixed fragment size
 * 
 * For cross-system shared memory usage with strict size regulations, each fragment 
 * occupies a fixed size regardless of actual payload:
 * - Each fragment = sizeof(ProxyMsgHeader) + PROXY_MSG_MAX_SIZE
 * - Total memory = number of fragments × fixed fragment size
 * 
 * Number of fragments is calculated using ceiling division to ensure all data is covered.
 * 
 * @param data_size Size of the data payload to be sent (in bytes)
 * @return size_t Total shared queue memory required (in bytes)
 */
#define PROXY_MSG_TOTAL_MEM_SIZE(data_size) \
    ( \
        /* Calculate number of fragments (ceiling division) */ \
        ( ((data_size) + PROXY_MSG_MAX_SIZE - 1) / PROXY_MSG_MAX_SIZE ) \
        * (sizeof(ProxyMsgHeader) + PROXY_MSG_MAX_SIZE) /* Fixed size per fragment */ \
    )


typedef enum {
    ACTION_TYPE_COMMAND = 0,  // Command
    ACTION_TYPE_RESPONSE      // Response
} ActionType;


/**
 * @brief Device message header structure
 * 
 * This structure defines the header format for device-related messages, containing metadata such as
 * protocol version, message type, message identifier, signaling type, and payload length.
 */
typedef struct {
    uint16_t version;        // Protocol version. Currently unused; set to 1.
    uint16_t msg_type;       // Message type. Possible values: Disable (0), Enable (1), Query (2)
    uint16_t msg_id;         // Message ID. Used to match commands with their corresponding responses.
    uint16_t action_type;    // Signaling type. Possible values: Command (0), Response (1)
    uint16_t payload_len;    // Payload length in bytes.
} __attribute__((packed)) DevMsgHeader;


typedef enum {
    DEV_MSG_DISABLE = 0,  // Disable
    DEV_MSG_ENABLE,       // Enable
    DEV_MSG_QUERY         // Query
} DevMsgType;

//  Check if the device message type is valid
#define IS_VALID_DEV_MSG_TYPE(dev_msg_type) \
    ((dev_msg_type) == DEV_MSG_DISABLE || \
     (dev_msg_type) == DEV_MSG_ENABLE || \
     (dev_msg_type) == DEV_MSG_QUERY)


/**
 * @brief Device message mask structure
 * 
 * Used to store the content of the "Enable"/"Disable" commands, specifically representing the mask 
 * that indicates which devices are selected for enabling or disabling.
 */
typedef struct {
    uint16_t data;   // Content of the "Enable"/"Disable" commands, representing the mask that indicates which devices are selected to enable or disable.
} __attribute__((packed)) DevMsgMask;



/**
 * @brief Device message report structure
 * 
 * This structure contains the response data for the "Query" command, including 
 * status information, error details, and the active device mask.
 */
typedef struct {
    uint8_t status;    // Status code indicating the overall result of the operation
    uint8_t error;     // Error code providing specific details about any errors encountered
    uint16_t data;     // Response data from the "Query" command, returning the mask indicating which devices are active
} __attribute__((packed)) DevMsgReport;


/**
 * Calculate the payload length of a device message based on its type and action type.
 * 
 * @param dev_msg_type    Device message type (DEV_MSG_ENABLE, DEV_MSG_DISABLE or DEV_MSG_QUERY)
 * @param action_type     Action type (ACTION_TYPE_COMMAND or ACTION_TYPE_RESPONSE)
 * @return                Payload length in bytes, or PROXY_MSG_INVALID_LEN if type is invalid
 */
#define DEV_MSG_PAYLOAD_LEN(dev_msg_type, action_type) \
( \
    /* Check if device message type is valid */ \
    (dev_msg_type == DEV_MSG_ENABLE) ? \
        ( \
            /* Check if action type is valid */ \
            (action_type == ACTION_TYPE_COMMAND)  ? 2 : \
            (action_type == ACTION_TYPE_RESPONSE) ? 4 : \
            PROXY_MSG_INVALID_LEN  /* Invalid action type */ \
        ) : \
    (dev_msg_type == DEV_MSG_DISABLE) ? \
        ( \
            (action_type == ACTION_TYPE_COMMAND)  ? 2 : \
            (action_type == ACTION_TYPE_RESPONSE) ? 2 : \
            PROXY_MSG_INVALID_LEN  /* Invalid action type */ \
        ) : \
    (dev_msg_type == DEV_MSG_QUERY) ? \
        ( \
            (action_type == ACTION_TYPE_COMMAND)  ? 0 : \
            (action_type == ACTION_TYPE_RESPONSE) ? 4 : \
            PROXY_MSG_INVALID_LEN  /* Invalid action type */ \
        ) : \
    PROXY_MSG_INVALID_LEN  /* Invalid device message type */ \
)


/* 
 * Get payload length directly from DevMsgHeader struct
 * euses DEV_MSG_PAYLOAD_LEN to avoid duplicate logic
 */
#define DEV_MSG_HEADER_PAYLOAD_LEN(header) \
    DEV_MSG_PAYLOAD_LEN((header)->msg_type, (header)->action_type)


/**
 * @brief Strategy message header structure
 * 
 * This structure defines the header format for strategy-related messages, containing metadata such as
 * protocol version, message type, message identifier, signaling type, and payload length.
 */
typedef struct {
    uint16_t version;       // Protocol version. Currently unused; set to 1.
    uint16_t msg_type;      // Message type. Possible values: Set (0), Query (1)
    uint16_t msg_id;        // Message ID. Used to match commands with their corresponding responses.
    uint16_t action_type;   // Signaling type. Possible values: Command (0), Response (1)
    uint16_t payload_len;   // Payload length in bytes.
} __attribute__((packed)) StrgyMsgHeader;


typedef enum {
    STRGY_MSG_SET = 0,       // Set
    STRGY_MSG_QUERY          // Query
} StrgyMsgType;


typedef enum {
    STRGY_OP_STATUS_SUCCESS = 0,  // Session operation succeeded
    STRGY_OP_STATUS_FAIL    = 1,   // Session operation failed
    STRGY_OP_STATUS_NUM     = 2   // Total number of enumeration members
} StrgyOpStatus;


// Corresponds to the code field in the structure, indicating specific reason codes for strategy operation results
typedef enum {
    STRGY_OP_CODE_SUCCESS                = 0,  // Operation succeeded
    STRGY_OP_CODE_PARAMETER_INVALID      = 1,  // Invalid Parameter
    STRGY_OP_CODE_MAX                          // Total number of enumeration members
} StrgyOpCode;


// Check if the strategy message type is valid
#define IS_VALID_STRGY_MSG_TYPE(strgy_msg_type) \
    ((strgy_msg_type) == STRGY_MSG_SET || \
     (strgy_msg_type) == STRGY_MSG_QUERY)


/**
 * Calculate the payload length of a strategy message based on its type and action type.
 * 
 * @param strgy_msg_type  Strategy message type (STRGY_MSG_SET or STRGY_MSG_QUERY)
 * @param action_type     Action type (ACTION_TYPE_COMMAND or ACTION_TYPE_RESPONSE)
 * @return                Payload length in bytes, or PROXY_MSG_INVALID_LEN if type is invalid
 */
#define STRGY_MSG_PAYLOAD_LEN(strgy_msg_type, action_type) \
( \
    /* Check strategy message type first */ \
    (strgy_msg_type == STRGY_MSG_SET) ? \
        ( \
            /* Determine length for SET message based on action type */ \
            (action_type == ACTION_TYPE_COMMAND)  ? 2 :  /* SET command → 2 bytes */ \
            (action_type == ACTION_TYPE_RESPONSE) ? 4 :  /* SET response → 4 bytes */ \
            PROXY_MSG_INVALID_LEN  /* Invalid action type for SET message */ \
        ) : \
    (strgy_msg_type == STRGY_MSG_QUERY) ? \
        ( \
            /* Determine length for QUERY message based on action type */ \
            (action_type == ACTION_TYPE_COMMAND)  ? 0 :  /* QUERY command → 0 bytes */ \
            (action_type == ACTION_TYPE_RESPONSE) ? 4 :  /* QUERY response → 4 bytes */ \
            PROXY_MSG_INVALID_LEN  /* Invalid action type for QUERY message */ \
        ) : \
    PROXY_MSG_INVALID_LEN  /* Invalid strategy message type */ \
)


/* 
 * Get payload length directly from StrgyMsgHeader struct
 * euses STRGY_MSG_PAYLOAD_LEN to avoid duplicate logic
 */
#define STRGY_MSG_HEADER_PAYLOAD_LEN(header) \
    STRGY_MSG_PAYLOAD_LEN((header)->msg_type, (header)->action_type)


/**
 * @brief Strategy command enable message structure
 * 
 * This structure defines the format for strategy command enable messages, specifically containing
 * parameters required when enabling a specified strategy. It is used to传递 configuration details
 * for strategy activation.
 */
typedef struct {
    uint16_t        strgy_para;    // Strategy parameter. Possible values: 0 (Round Robin), 1 (Select device with highest current available bandwidth), 2 (Select device with lowest current latency)
} __attribute__((packed)) StrgyCMDEnableMessage;


/**
 * @brief Strategy message report structure
 * 
 * This structure contains the response data for strategy-related "Query" commands, including
 * a status code, error code, and the active strategy information returned by the query.
 */
typedef struct {
    uint8_t    status;      // Status code indicating the overall result of the strategy operation (e.g., success or failure)
    uint8_t    error;       // Error code providing specific details if the strategy operation encountered an error (0 for no error)
    uint16_t   data[];      // Flexible array member acting as a placeholder. Stores the response data from the "Query" command, specifically the active strategy code.
} __attribute__((packed)) StrgyMsgReport;


/**
 * @brief Session message header structure
 * 
 * This structure defines the header format for session-related messages, containing metadata such as
 * protocol version, message type, signaling type, IP version, and payload length. It is used for
 * managing session operations like creation and closure.
 */
typedef struct {
    uint16_t   version;        // Protocol version. Currently unused; set to 1.
    uint16_t   msg_type;       // Message type. Possible values: Create (0), Close (1)
    uint16_t   action_type;    // Signaling type. Possible values: Command (0), Response (1)
    uint16_t   ip_version;     // IP version. Possible values: SESS_IPV4_PROTO (4), SESS_IPV6_PROTO (6)
    uint16_t   payload_len;    // Payload length in bytes.
} __attribute__((packed)) SessMsgHeader;



/**
 * @brief IPv4 session parameter structure
 * 
 * This structure contains parameters required for establishing or managing an IPv4-based session,
 * including device identification, transport protocol, IPv4 address, and corresponding port information.
 */
typedef struct{
    uint16_t            dev_id;         // Device identifier, uniquely identifies the target device in the session
    uint16_t            trans_proto;    // Transport protocol used for the session (e.g., TCP, UDP)
    struct IPv4Address  ipv4_addr;      // IPv4 address structure containing the device's IPv4 address information
    uint16_t            port;           // Port number associated with the IPv4 address for the session
} __attribute__((packed)) SessParaIPv4;



/**
 * @brief IPv6 session parameter structure
 * 
 * This structure contains parameters required for establishing or managing an IPv6-based session,
 * including device identification, transport protocol, IPv6 address, and corresponding port information.
 */
typedef struct{
    uint16_t            dev_id;         // Device identifier, uniquely identifies the target device in the session
    uint16_t            trans_proto;    // Transport protocol used for the session (e.g., TCP, UDP)
    struct IPv6Address  ipv6_addr;      // IPv6 address structure containing the device's IPv6 address information
    uint16_t            port;           // Port number associated with the IPv6 address for the session
} __attribute__((packed)) SessParaIPv6;


typedef enum {
    SESS_MSG_CLOSE = 0,             // Close
    SESS_MSG_CREATE                 // Create
} SessMsgType;


typedef enum {
    SESS_NON_IP_PROTO = 0,       // None-IP protocol
    SESS_IPV4_PROTO   = 4,       // IPv4
    SESS_IPV6_PROTO   = 6        // IPv6
} SessIpProtoVersion;


typedef enum {
    SESS_UDP_PROTO = 0,            // UDP
    SESS_TCP_PROTO = 1,            // TCP
    SESS_FASTPATH_PROTO = 2        // XDP or eBPF
} SessTranProto;

// Check if the session message type is valid
#define IS_VALID_SESS_MSG_TYPE(sess_msg_type) \
    ((sess_msg_type) == SESS_MSG_CREATE || \
     (sess_msg_type) == SESS_MSG_CLOSE)


// Check if the IP protocol verion is valid
#define IS_VALID_SESS_IP_VERSION(ip_version) \
    ((ip_version) == SESS_IPV4_PROTO || \
     (ip_version) == SESS_IPV6_PROTO)



/**
 * Calculate the payload length of a session message based on its type, action type, and IP protocol version.
 * 
 * @param sess_msg_type  Session message type (SESS_MSG_CREATE or SESS_MSG_CLOSE)
 * @param action_type    Action type (ACTION_TYPE_COMMAND or ACTION_TYPE_RESPONSE)
 * @param ip_version     IP protocol version (SESS_IPV4_PROTO or SESS_IPV6_PROTO)
 * @return               Payload length in bytes, or PROXY_MSG_INVALID_LEN if type/version is invalid
 */
#define SESS_MSG_PAYLOAD_LEN(sess_msg_type, action_type, ip_version) \
( \
    /* Check session message type first */ \
    (sess_msg_type == SESS_MSG_CLOSE) ? \
        ( \
            utils_print("type is SESS_MSG_CLOSE\n"),\
            /* Determine length for CLOSE message based on action type */ \
            (action_type == ACTION_TYPE_COMMAND)  ? 0 :  /* CLOSE command → 0 bytes */ \
            (action_type == ACTION_TYPE_RESPONSE) ? 2 :  /* CLOSE response → 2 bytes (status + error) */ \
            PROXY_MSG_INVALID_LEN  /* Invalid action type for CLOSE message */ \
        ) : \
    (sess_msg_type == SESS_MSG_CREATE) ? \
        ( \
            utils_print("type is SESS_MSG_CREATE, action type is %d, ip_version is %d\n",action_type,  ip_version),\
            /* Determine length for CREATE message based on action type */ \
            (action_type == ACTION_TYPE_RESPONSE) ? 2 :  /* CREATE response → 2 bytes (status + error) */ \
            (action_type == ACTION_TYPE_COMMAND)  ? \
                ( \
                    /* Determine length for CREATE command based on IP version */ \
                    (ip_version == SESS_IPV4_PROTO) ? 10 :  /* IPv4 → 10 bytes (session params) */ \
                    (ip_version == SESS_IPV6_PROTO) ? 22 :  /* IPv6 → 22 bytes (session params) */ \
                    PROXY_MSG_INVALID_LEN  /* Invalid IP version for CREATE command */ \
                ) : \
            PROXY_MSG_INVALID_LEN  /* Invalid action type for CREATE message */ \
        ) : \
    PROXY_MSG_INVALID_LEN  /* Invalid session message type */ \
)

/* 
 * Get payload length directly from StrgySessHeader struct
 * euses STRGY_MSG_PAYLOAD_LEN to avoid duplicate logic
 */
#define SESS_MSG_HEADER_PAYLOAD_LEN(header) \
    SESS_MSG_PAYLOAD_LEN((header)->msg_type, (header)->action_type, (header)->ip_version)

typedef struct {
    struct IPv4Address  ipv4_addr;       // IPv4 address
    uint16_t            port;   	     // Transport layer port；
} __attribute__((packed)) IPv4PortTuple;

typedef struct {
    struct IPv6Address  ipv6_addr;       // IPv6 address
    uint16_t            port;   	                // Transport layer port
} __attribute__((packed)) IPv6PortTuple;

typedef union {
    IPv4PortTuple ipv4_port_tuple;
    IPv6PortTuple ipv6_port_tuple;
}IPPortTuple;

/*
 * The structure of the session create-response message's payload.
 */
// Corresponds to the status field in the structure, indicating the overall status of the session operation (success/failure)
typedef enum {
    SESS_OP_STATUS_SUCCESS = 0,  // Session operation succeeded
    SESS_OP_STATUS_FAIL    = 1,   // Session operation failed
    SESS_OP_STATUS_NUM     = 2   // Total number of enumeration members
} SessOpStatus;


// Corresponds to the code field in the structure, indicating specific reason codes for operation results
typedef enum {
    SESS_OP_CODE_SUCCESS                = 0,  // Operation succeeded
    SESS_OP_CODE_NO_PERMISSION          = 1,  // No permission to perform the operation
    SESS_OP_CODE_DEVICE_ERROR           = 2,  // Device error occurred
    SESS_OP_CODE_RESOURCE_INSUFFICIENT  = 3,  // Insufficient resources
    SESS_OP_CODE_NETWORK_UNREACHABLE    = 4,  // Network is unreachable
    SESS_OP_CODE_PARAMETER_INVALID      = 5,  // Invalid Parameter
    SESS_OP_CODE_MAX                          // Total number of enumeration members
} SessOpCode;


// Structure for session operation response data, containing status and specific reason code
typedef struct SessOpRespData_ {
    uint8_t             status;  // Corresponding to SessOpStatus enumeration (overall operation status)
    uint8_t             code;    // Corresponding to SessOpCode enumeration (specific reason code for operation result)
} __attribute__((packed)) SessOpRespData;

/*
 * Session message parameter structure, used to describe core parameters related to session establishment,
 * including device identification, transport protocol type, and IP-port combination and other key information.
 */
struct SessMsgPara{
// Frontend session ID, used to deliver information for establishing a new session.
    uint16_t        frontend_sess_id;
// Backend session ID, used to deliver information for establishing a new session.
    uint16_t        backend_sess_id;
// Device ID, of type uint16_t, with value range 0x0000-0xFFFF; when set to 0xFFFF, it indicates entering vertical handover mode.
    uint16_t        dev_id;
// IP version, of type uint16_t, of type uint16_t, supported values include: 4 (IPv4), 6 (IPv6).
    uint16_t        ip_version;
// Transport layer protocol type, of type uint16_t, supported values include: SESS_UDP_PROTO (0, UDP protocol), SESS_TCP_PROTO(1, TCP protocol), SESS_FASTPATH_PROTO(2, FastPath protocol).
    uint16_t        trans_proto;
// Tuple of IP address and port number, used to describe the combination of IP address and corresponding port number of the communication endpoint.
    IPPortTuple     ip_port_tuple;
};


/**
 * @brief Session parameters structure for IPv4-based sessions
 * @details Contains parameters required to establish and manage an IPv4 session,
 *          including transport protocol, device selection, and destination endpoint.
 *          Uses packed alignment to ensure contiguous memory layout.
 */
typedef struct {
//    uint16_t            transport_layer_proto;  /**< Transport layer protocol (2 bytes, e.g., SESS_TCP_PROTO for TCP, SESS_UDP_PROTO for UDP) */
    uint16_t            device_selection;       /**< Device selection identifier (2 bytes) */
    uint16_t            transport_layer_proto;  /**< Transport layer protocol (2 bytes, e.g., SESS_TCP_PROTO for TCP, SESS_UDP_PROTO for UDP) */
    IPv4PortTuple       dest_endpoint;          /**< Destination endpoint containing IPv4 address and port */
} __attribute__((packed)) SessIPv4Params;



/**
 * @brief Session parameters structure for IPv6-based sessions
 * @details Contains parameters required to establish and manage an IPv6 session,
 *          including transport protocol, device selection, and destination endpoint.
 *          Uses packed alignment to ensure contiguous memory layout.
 */
typedef struct {
//    uint16_t            transport_layer_proto;  /**< Transport layer protocol (2 bytes, e.g., 6 for TCP, 17 for UDP) */
    uint16_t            device_selection;       /**< Device selection identifier (2 bytes) */
    uint16_t            transport_layer_proto;  /**< Transport layer protocol (2 bytes, e.g., 6 for TCP, 17 for UDP) */
    IPv6PortTuple       dest_endpoint;          /**< Destination endpoint containing IPv6 address and port */
} __attribute__((packed)) SessIPv6Params;



/**
 * @brief IoT protocol type enumeration for IoT message sub-header
 * @note Used to distinguish different IoT protocols in PROXY_MSG_TYPE_IOT payload
 */
typedef enum {
    IOT_PROTO_TYPE_UNKNOWN,
    IOT_PROTO_TYPE_BLUETOOTH = 1,  // Bluetooth protocol (BLE/Classic Bluetooth)
    IOT_PROTO_TYPE_ZIGBEE,         // Zigbee protocol (802.15.4)
    IOT_PROTO_TYPE_CAN,            // CAN bus protocol (CAN 2.0/CAN FD)
    IOT_PROTO_TYPE_LORA,           // LoRa/LoRaWAN protocol
    IOT_PROTO_TYPE_POWERLINK,      // OpenPowerLink (Ethernet POWERLINK) protocol
    IOT_PROTO_TYPE_MODBUSTCP       // OpenPowerLink (Ethernet POWERLINK) protocol
} IotProtoType;

/**
 * @brief IoT message operation code enumeration
 * @note Defines universal operation semantics for IoT message interaction
 */
typedef enum {
    IOT_OPCODE_DATA_REPORT = 0,    // Device actively reports data (receive direction)
    IOT_OPCODE_CMD_DOWNLINK,       // Backend sends control command (send direction)
    IOT_OPCODE_DEVICE_DISCOVER,    // Discover IoT devices in the network
    IOT_OPCODE_DEVICE_REGISTER,    // Device network access/registration
    IOT_OPCODE_STATUS_QUERY,       // Query device status (connection/signal/power)
    IOT_OPCODE_PROTO_CONFIG,       // Configure IoT protocol parameters
    IOT_OPCODE_EXCEPTION_NOTIFY    // Report protocol/device exception events
} IotOpcode;

/**
 * @brief IoT message header (for PROXY_MSG_TYPE_IOT payload)
 * @note Fixed length: 10 bytes (packed structure)
 * @note Field order: proto_ver -> proto_type -> opcode -> dev_port_id -> payload_len -> reserve
 */
typedef struct IotMsgHeader_{
    uint16_t    proto_ver;              // IoT sub-protocol version (fixed: 0x01 for current version)
    uint16_t    proto_type;             // IoT protocol type (Bluetooth/Zigbee/CAN/LoRa/POWERLINK/MODBUS)
    uint16_t    opcode;                 // IoT message operation code
    uint16_t    dev_port_id;            // Device/port ID (to distinguish multi-port/device)
    uint16_t    payload_len;            // Length of IoT protocol raw data payload
    uint16_t    reserve;                // Reserved field (fixed: 0x0000)
} __attribute__((packed)) IotMsgHeader;


/* -------------------------- IoT Protocol Address Structures -------------------------- */
/**
 * @brief Bluetooth device address (MAC + port/channel)
 */
typedef struct {
    uint8_t  mac[18];        /**< Bluetooth MAC string (e.g., "AA:BB:CC:DD:EE:FF" + '\0') */
    uint16_t port;       /**< Bluetooth port/channel number (PSM or CID) */
} __attribute__((packed)) IotBtAddr;


/**
 * @brief CAN device address (port + CAN ID + bus ID)
 */
typedef struct {
    uint16_t port;       /**< CAN port number */
    uint32_t can_id;     /**< CAN frame ID (11/29-bit) */
    uint8_t bus_id;      /**< CAN bus ID (multi-bus system) */
} __attribute__((packed))IotCanAddr;

/**
 * @brief ZigBee device address (64-bit MAC + PAN ID + endpoint)
 */
typedef struct {
    uint8_t mac[8];      /**< ZigBee 64-bit extended MAC address */
    uint16_t pan_id;     /**< ZigBee PAN ID */
    uint8_t endpoint;    /**< ZigBee endpoint (0~255) */
} __attribute__((packed))IotZigbeeAddr;

/**
 * @brief LoRa device address (DevEUI + port + frequency band)
 */
typedef struct {
    uint64_t dev_eui;    /**< LoRa unique device EUI */
    uint16_t port;       /**< LoRa application port */
    uint8_t freq_band;   /**< LoRa frequency band (EU868/US915/CN470) */
} __attribute__((packed))IotLoraAddr;

/**
 * @brief PowerLink device address (NodeID + MAC + PDO ID)
 */
typedef struct {
    uint16_t node_id;    /**< PowerLink node ID (1~240) */
    uint8_t mac[6];      /**< PowerLink MAC address */
    uint16_t pdo_id;     /**< PDO object identifier */
} __attribute__((packed))IotPowerLinkAddr;


/**
 * @brief Modbus TCP device address (IPv4 + Port)
 * 
 * Note: Modbus TCP addressing relies on the TCP/IP stack.
 * - IP Address: Target server/client IPv4 address.
 * - Port: Typically 502 (standard), but can be custom.
 * - Unit ID is usually part of the PDU payload, not the transport address, 
 *   so it is not included here to keep the struct compact.
 */
typedef struct {
    uint8_t ip[4];       /**< IPv4 address (e.g., 192.168.1.10) */
    uint16_t port;       /**< TCP port number (Default: 502) */
    uint8_t  unit_id;      /**< Modbus unit ID (slave ID) */
    uint16_t reg_addr;     /**< Start address of target register */
    uint16_t reg_num;      /**< Total registers for read/write operations */
} __attribute__((packed)) IotModbusTcpAddr;


typedef enum {
    IOT_MODBUS_CMD_WRITE_REQ  = 0,  /**< Write request command */
    IOT_MODBUS_CMD_WRITE_RESP = 1,  /**< Write response command */
    IOT_MODBUS_CMD_READ_REQ   = 2,  /**< Read request command */
    IOT_MODBUS_CMD_READ_RESP  = 3   /**< Read response command */
} IotModbusCmd;

typedef struct {
    uint8_t  cmd;          /* 0: WRITE-CMD, 1: WRITE-RESP; 2: READ-CMD, 3: READ_RESP */
    uint16_t value;        /* value to be sent or receive */
}__attribute__((packed)) IotModbusTcpMsg;
/* -------------------------- Unified IoT Address Structure -------------------------- */
/**
 * @brief Unified IoT device address structure (type + union address)
 */
/**
 * @brief Unified IoT device address structure (type + union address)
 */
typedef struct IotAddr_ {
    IotProtoType addr_type;      /**< Protocol type (matches session type) */
    union {
        IotBtAddr          bt_addr;        /**< Bluetooth address */
        IotCanAddr         can_addr;       /**< CAN address */
        IotZigbeeAddr      zigbee_addr;    /**< ZigBee address */
        IotLoraAddr        lora_addr;      /**< LoRa address */
        IotPowerLinkAddr   powerlink_addr; /**< PowerLink address */
        IotModbusTcpAddr   modbus_tcp_addr;/**< Modbus TCP address (New) */
        uint8_t            raw[16];        /**< Raw fallback bytes (max 16 bytes) */
    } addr_info;
} IotAddr;

typedef struct IotMsgBuffer_ {
    uint8_t     *data;           /**< Pointer to raw data buffer */
    uint32_t    len;             /**< Length of data (bytes) */
    uint32_t    msg_id;          /**< Unique message ID (for tracking) */
    uint64_t    timestamp;       /**< Message timestamp (ms since epoch) */
    IotAddr     addr;            /**< Address info: 
                                  - Send: Destination address
                                  - Receive: Source address */
    void        *ext_info;       /**< Protocol-specific extended info (beyond address) */
} IotMsgBuffer;

typedef struct GeneralProxyMsgHeader_{
    ProxyMsgHeader  outer_header;
//    ProxyMsgType    msg_type;
    union {        // Nested union to reduce memory usage (avoids redundant space)
        DevMsgHeader        dev_hdr;    // Device message header member
        StrgyMsgHeader      strgy_hdr;  // Strategy message header member
        SessMsgHeader       sess_hdr;   // Session message header member
        IotMsgHeader        iot_hdr;    // IoT message header member
    } inner_header; // Nested union alias for easy access to specific headers
    IotAddr         iot_addr;              /**< IoT address (ONLY used for IoT messages; 0-initialized for non-IoT)
                                             - Stores bt_addr/can_addr/zigbee_addr/lora_addr/powerlink_addr
                                             - Size: 18 bytes (IotProtoType + 16-byte union) → minimal overhead */
    uint16_t        iot_addr_len;          /**< Length of valid IoT address data (bytes)
                                             - 0: Non-IoT message (ignore iot_addr)
                                             - >0: IoT message (valid address length for specific protocol) */
} GeneralProxyMsgHeader;

struct FrontendEngine_;
struct SharedMemoryPoolQueue;

typedef enum {
    MEMORY_ALLOC_SHARED,    // Allocate in shared memory
    MEMORY_ALLOC_CALLER,    // Memory is allocated by the caller
    MEMORY_ALLOC_AMPQUEUE   // Message is placed directly into the HyperAMP queue
} MemoryAllocMode;


/* -------------------------------------------------------------------------- */
/*                         Protocol Payload Limits                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Maximum pure payload for Bluetooth (Classic/BLE).
 * 
 * Standards Reference:
 * - BLE: Default ATT MTU is 23 bytes (20 bytes payload). With MTU exchange, 
 *   it can go up to 247 bytes (244 bytes payload).
 * - Classic Bluetooth: Varies by L2CAP configuration.
 * 
 * Configured Value: 244 bytes (Assumes BLE MTU negotiation enabled).
 * Adjust to 20 if operating in legacy BLE mode without MTU exchange.
 */
#define FRONTEND_BLUETOOTH_MAX_PAYLOAD     (244)

/**
 * @brief Maximum pure payload for ZigBee (IEEE 802.15.4).
 * 
 * Standards Reference:
 * - IEEE 802.15.4 MAC frame max is 127 bytes.
 * - After subtracting MAC, Network (NWK), and APS headers, the APS payload 
 *   typically allows ~80 to 100 bytes without fragmentation.
 * 
 * Configured Value: 100 bytes (Conservative limit to avoid fragmentation).
 */
#define FRONTEND_ZIGBEE_MAX_PAYLOAD        (100)

/**
 * @brief Maximum pure payload for LoRa / LoRaWAN.
 * 
 * Standards Reference:
 * - LoRaWAN payload size depends on Spreading Factor (SF) and regional regulations.
 * - Typical max payload ranges from 51 bytes (SF12) to 243 bytes (SF7).
 * 
 * Configured Value: 222 bytes (Safe upper bound for most SF settings).
 * Reduce to ~50-60 bytes for long-range/low-data-rate scenarios (SF11/SF12).
 */
#define FRONTEND_LORA_MAX_PAYLOAD          (222)

/**
 * @brief Maximum pure payload for PowerLink (Power Line Communication).
 * 
 * Standards Reference:
 * - Depends on specific PLC chipset (e.g., G3-PLC, PRIME, or proprietary).
 * - High noise environments often require smaller frames for reliability.
 * 
 * Configured Value: 128 bytes (Typical value for many PLC applications).
 * Verify against specific hardware datasheet.
 */
#define FRONTEND_POWERLINK_MAX_PAYLOAD     (128)

/**
 * @brief Maximum pure payload for CAN bus.
 * 
 * Standards Reference:
 * - CAN 2.0 (Standard/Extended): Max 8 bytes data field.
 * - CAN FD (Flexible Data-rate): Max 64 bytes data field.
 * 
 * Configured Value: 64 bytes (Assumes CAN FD support).
 * IMPORTANT: Change to 8 if using legacy CAN 2.0 hardware.
 */
#define FRONTEND_CAN_MAX_PAYLOAD           (64)

/**
 * @brief Maximum pure payload for Modbus TCP.
 * 
 * Standards Reference:
 * - Modbus TCP ADU (Application Data Unit) is encapsulated in TCP.
 * - The MBAP header is 7 bytes.
 * - While TCP allows large segments, standard Modbus implementations often 
 *   limit the PDU (Protocol Data Unit) to 253 bytes (0x00FD) to ensure 
 *   compatibility with embedded devices and avoid TCP fragmentation issues.
 * - Theoretical max is 65535, but 253 is the de-facto standard safe limit.
 * 
 * Configured Value: 253 bytes (Standard Modbus TCP PDU limit).
 */
#define FRONTEND_MODBUS_TCP_MAX_PAYLOAD    (253)

int build_proxy_general_message(struct FrontendEngine_ *engine, GeneralProxyMsgHeader *header, const uint8_t *payload, size_t payload_len, 
                                uint8_t **result_msg, MemoryAllocMode alloc_mode, struct SharedMemoryPoolQueue *ring_buf);
int build_proxy_dev_message(DevMsgHeader *header, const uint8_t *payload, size_t payload_len, uint8_t **result_msg);
int build_proxy_strgy_message(StrgyMsgHeader *header, const uint8_t *payload, size_t payload_len, uint8_t **result_msg);
int build_proxy_sess_message(SessMsgHeader *header, const uint8_t *payload, size_t payload_len, uint8_t **result_msg);
int build_proxy_data_message(ProxyMsgHeader *header, const uint8_t *payload, size_t payload_len, uint8_t **result_msg);

/**
 * @brief Build IoT proxy message (supports 3-layer structure: ProxyMsgHeader + IotMsgHeader + IotAddr + payload)
 * 
 * Core construction function for IoT proxy messages (called by build_proxy_general_message):
 * 1. Validates IotMsgHeader (proto_type/opcode/payload_len) and IotAddr (addr_type matches proto_type)
 * 2. Calculates total length: sizeof(ProxyMsgHeader) + sizeof(IotMsgHeader) + header->iot_addr_len + payload_len
 * 3. Allocates memory for the full message (heap/pool based on caller context)
 * 4. Writes data in order:
 *    - ProxyMsgHeader (from header->outer_header)
 *    - IotMsgHeader (from header->inner_header.iot_hdr)
 *    - IotAddr (protocol-specific address from header->iot_addr, truncated to header->iot_addr_len)
 *    - Payload (raw data from payload parameter)
 * 5. Updates ProxyMsgHeader.total_len with the full message length
 * 
 * @param header Pointer to GeneralProxyMsgHeader (must contain valid iot_hdr + iot_addr + iot_addr_len)
 * @param payload Pointer to IoT payload (raw data after address; NULL if no payload)
 * @param payload_len Length of IoT payload (bytes; 0 if no payload)
 * @param result_msg Output pointer to constructed IoT proxy message (allocated internally)
 * @return int Construction result
 *         - FRONTEND_PROXY_PROCESS_OK: IoT message built successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid parameters/address/protocol mismatch/memory error)
 * 
 * @note Validates protocol consistency: header->inner_header.iot_hdr.proto_type must match header->iot_addr.addr_type
 * @note header->iot_addr_len must be >0 and ≤ sizeof(IotAddr) (e.g., 8 for bt_addr, 7 for can_addr)
 * @note IotMsgHeader.payload_len is automatically set to (header->iot_addr_len + payload_len)
 * @note Handles frontend-to-backend IoT messages transmitted via HyperAMP (same as other proxy messages)
 */
int build_proxy_iot_message(GeneralProxyMsgHeader *header, 
                            const uint8_t *payload, 
                            size_t payload_len, 
                            uint8_t **result_msg);

#endif