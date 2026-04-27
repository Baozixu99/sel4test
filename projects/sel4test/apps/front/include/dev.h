#ifndef DEV_H_
#define DEV_H_

#include <stdint.h>

#define MAX_DEV_NAME                            32
#define MAX_HS_DEV_NUM                          16
#define MIN_HS_DEV_NUM                          1

#define MAX_IOT_DEV_NUM                         16
/*
 * High Speed Network device type enumeration
 * Contains five common types of network devices
 */
typedef enum {
    TRADITIONAL_ETHERNET = 0,   // Traditional Ethernet device
    TSN,                        // Time-Sensitive Networking device
    WIFI,                       // WiFi wireless network device
    LTE_MODULE,                 // 4G module (LTE technology standard)
    NR_MODULE                   // 5G module (NR technology standard)
} HSNetDevType;


/**
 * @brief Frontend device basic information structure
 * @details This structure stores the core configuration and status parameters of a single frontend device,
 *          including unique identifier, type, running status and human-readable device name.
 *          It is used as the basic data unit in the device list management module.
 * @note The name array is a fixed-length character buffer, and the valid string must be terminated with a null character ('\0').
 * @note The values of dev_type and dev_status are usually defined by enumerated types or global macros for unified management.
 */
typedef struct FrontendDevInfo_{
    int                 dev_id;
    int                 dev_type;
    int                 dev_status;
    char                name[MAX_DEV_NAME];
}FrontendDevInfo;


/**
 * @brief Configuration structure for frontend device list management
 * @details This structure aggregates the global configuration data for a set of frontend devices. It defines the total number of devices and references a contiguous array
 *          that stores the detailed configuration of each individual device.
 * @note The memory of the array pointed by @p dev_info can be allocated statically, globally, on the stack, or dynamically. Memory management (allocation and deallocation)
 *       is the responsibility of the caller. The number of valid array elements must match the value of @p dev_num to avoid out-of-bounds access.
 */
typedef struct FrontendDevListCfg_{
    int                 dev_num;
    FrontendDevInfo     *dev_info;
}FrontendDevListCfg;


typedef struct FrontendHighSpeedNetDeviceSet_ {
    FrontendDevInfo hs_net_dev[MAX_HS_DEV_NUM];
}FrontendHighSpeedNetDeviceSet;

extern FrontendDevListCfg *p_global_dev_list_cfg;

void frontend_init_dev_list();



/**
 * @brief IoT device type enumeration (matches IotProtoType in message definition)
 */
typedef enum {
    IOT_DEV_TYPE_UNKNOWN = 0,    // Unknown IoT device type
    IOT_DEV_TYPE_BLUETOOTH,      // Bluetooth device (BLE/Classic Bluetooth)
    IOT_DEV_TYPE_ZIGBEE,         // Zigbee device (802.15.4)
    IOT_DEV_TYPE_CAN,            // CAN bus device (CAN 2.0/CAN FD)
    IOT_DEV_TYPE_LORA,           // LoRa/LoRaWAN device
    IOT_DEV_TYPE_POWERLINK,      // OpenPowerLink (Ethernet POWERLINK) device
    IOT_DEV_TYPE_MODBUSTCP       //  Modbus TCP device (Modbus protocol over TCP/IP).
} IotDevType;

/**
 * @brief IoT device status enumeration
 */
typedef enum IotDevStatus_{
    IOT_DEV_STATUS_OFFLINE = 0,  // Device offline (disconnected/unavailable)
    IOT_DEV_STATUS_ONLINE,       // Device online (connected/available)
    IOT_DEV_STATUS_ERROR,        // Device error (fault/abnormal state)
    IOT_DEV_STATUS_CONFIGURING   // Device being configured (temporary state)
} IotDevStatus;


/**
 * @brief Bluetooth device specific parameters
 */
typedef struct BluetoothDevAttr_{
    uint16_t    bt_port;         // Bluetooth port/channel number
    uint8_t     bt_mac[6];       // Bluetooth MAC address (6 bytes)
    uint8_t     bt_version;      // Bluetooth version (0x01=BLE 5.0, 0x02=BLE 5.1, etc.)
    uint16_t    conn_interval;   // BLE connection interval (unit: ms)
} BluetoothDevAttr;

/**
 * @brief CAN device specific parameters
 */
typedef struct CANDevAttr_{
    uint16_t    can_port;        // CAN port number
    uint32_t    can_bitrate;     // CAN bus bitrate (bps, e.g., 500000 for 500Kbps)
    uint8_t     can_mode;        // CAN mode (0x01=normal, 0x02=loopback, 0x03=silent)
    uint32_t    can_filter_id;   // CAN filter ID (for frame filtering)
} CANDevAttr;

/**
 * @brief Zigbee device specific parameters
 */
typedef struct ZigbeeDevAttr_{
    uint16_t    zigbee_pan_id;   // Zigbee PAN ID
    uint8_t     zigbee_channel;  // Zigbee channel (range: 11-26)
    uint8_t     zigbee_mac[8];   // Zigbee MAC address (8 bytes)
    uint8_t     zigbee_role;     // Zigbee role (0x01=coordinator, 0x02=router, 0x03=end device)
} ZigbeeDevAttr;

/**
 * @brief LoRa device specific parameters
 */
typedef struct LoRaDevAttr_{
    uint16_t    lora_port;       // LoRa port number
    uint8_t     lora_freq_band;  // LoRa frequency band (0x01=EU868, 0x02=US915, 0x03=CN470, etc.)
    uint8_t     lora_sf;         // LoRa spreading factor (range: 7-12)
    uint8_t     lora_cr;         // LoRa coding rate (range: 1-4, corresponds to 4/5 ~ 4/8)
    uint32_t    lora_dev_eui;    // LoRa device EUI (unique identifier)
} LoRaDevAttr;



/**
 * @brief OpenPowerLink device specific parameters (Industrial Real-Time Ethernet)
 */
typedef struct PowerLinkDevAttr_ {
    uint16_t    plk_port;        // POWERLINK port number (corresponding to Ethernet interface)
    uint8_t     plk_mac[6];      // Device MAC address (6 bytes)
    uint16_t    plk_node_id;     // POWERLINK NodeID (range: 1-240)
    uint8_t     plk_role;        // POWERLINK role (0: MN (Managing Node), 1: CN (Controlled Node))
    uint32_t    plk_cycle_ms;    // Real-time cycle time (unit: ms, typically 1~10ms)
    uint16_t    plk_rx_pdo_len;  // Length of received PDO (Process Data Object)
    uint16_t    plk_tx_pdo_len;  // Length of transmitted PDO (Process Data Object)
} PowerLinkDevAttr;

/**
 * @brief Union for IoT device specific attributes (memory optimization)
 */
typedef union {
    BluetoothDevAttr    bt_attr;     // Bluetooth device attributes
    CANDevAttr          can_attr;    // CAN device attributes
    ZigbeeDevAttr       zigbee_attr; // Zigbee device attributes
    LoRaDevAttr         lora_attr;   // LoRa device attributes
    PowerLinkDevAttr    plk_attr;    // OpenPowerLink device attributes
} IotDevSpecificAttr;

/**
 * @brief IoT device performance statistics
 */
typedef struct IotDevStat_{
    uint64_t    tx_packets;      // Total transmitted packets
    uint64_t    rx_packets;      // Total received packets
    uint64_t    tx_bytes;        // Total transmitted bytes
    uint64_t    rx_bytes;        // Total received bytes
    uint32_t    error_count;     // Total error count
    uint64_t    last_active_ts;  // Last active timestamp (ms since epoch)
} IotDevStat;


/**
 * @brief Main structure for IoT device (Bluetooth/CAN/Zigbee/LoRa/POWERLINK)
 * @note Refer to HighSpeedNetDevice design, optimized for IoT characteristics
 */
typedef struct IotDevice_ {
    // Device identification information
    int                         dev_id;             // Unique device ID (global)
    int                         dev_type;           // IoT device type, equal to protocol type
    char                        *ns_name;           // Namespace name (for device grouping)
    IotDevStatus                dev_status;         // Device online/offline status

    // Device attribute information
    char                        name[MAX_DEV_NAME]; // Device name (human-readable)
    IotDevSpecificAttr          specific_attr;      // Protocol-specific attributes

    // Session connection information
    int                         sess_id;            // Associated session ID (initialized on startup)

    // Performance statistics information
    IotDevStat                  stat;               // Device performance statistics

    // Device hardware/port information
    int                         physical_port;      // Physical port number (e.g., /dev/ttyUSB0 mapped to ID)
    int                         fd;                 // Device file descriptor (for hardware access)
} IotDevice;

typedef struct FrontendIoTDeviceSet_ {
    IotDevice iot_dev[MAX_IOT_DEV_NUM];
}FrontendIoTDeviceSet;

#endif