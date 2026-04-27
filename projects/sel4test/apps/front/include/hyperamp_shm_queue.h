/**
 * @file hyperamp_shm_queue.h
 * @brief HyperAMP Shared Memory Queue – seL4 version
 *
 * This is a streamlined seL4 version, copied and modified from the hvisor-tool project.
 */

#ifndef HYPERAMP_SHM_QUEUE_SEL4_H
#define HYPERAMP_SHM_QUEUE_SEL4_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "hardware_config.h"

/* ==================== Constant Definitions ==================== */

#define HYPERAMP_ERROR_ADDR             UINT64_MAX
#define HYPERAMP_MAX_MAP_TABLE_ENTRIES  125  /* Makes the queue control region exactly 4KB (1 page) */

#define HYPERAMP_OK                     0
#define HYPERAMP_ERROR                  (-1)
#define HYPERAMP_AGAIN                  1

#define HYPERAMP_ZONE_ID_ROOTLINUX       0
#define HYPERAMP_ZONE_ID_SEL4            1


/* Memory mapping modes */
typedef enum {
    HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH = 0,
    HYPERAMP_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL
} HyperampMapMode;

/* Message constants */
#define HYPERAMP_MSG_HDR_SIZE           8
#define HYPERAMP_MSG_MIN_SIZE           1
#define HYPERAMP_MSG_MAX_SIZE           4088
#define HYPERAMP_MSG_HDR_PLUS_MAX_SIZE  (HYPERAMP_MSG_HDR_SIZE + HYPERAMP_MSG_MAX_SIZE)

/* Queue magic number */
#define HYPERAMP_QUEUE_MAGIC            0x48415150  // "HAQP"


/* ==================== Memory Barriers and Cache Operations ==================== */

#if defined(__aarch64__) || defined(__arm__)
    #define HYPERAMP_DMB()   __asm__ volatile("dmb sy" ::: "memory")
    #define HYPERAMP_ISB()   __asm__ volatile("isb" ::: "memory")
    
    /* 缓存操作 - 仅用于数据区 (DEVICE_nGnRnE uncached)，队列控制块由 LDAXR/STLXR 处理一致性 */
    static inline void hyperamp_cache_clean(volatile void *addr, size_t size) {
        volatile char *p = (volatile char *)addr;
        volatile char *end = p + size;
        /* ARM64 cache line size is typically 64 bytes */
        for (; p < end; p += 64) {
            __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
        }
        __asm__ volatile("dsb sy" ::: "memory");
    }
    
    /* Data cache invalidate – discard cached contents and force reload from memory (used for shared memory reads) */
    static inline void hyperamp_cache_invalidate(volatile void *addr, size_t size) {
        volatile char *p = (volatile char *)addr;
        volatile char *end = p + size;
        for (; p < end; p += 64) {
            __asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
        }
        __asm__ volatile("dsb sy" ::: "memory");
    }
#else
    #define HYPERAMP_DMB()   __asm__ volatile("mfence" ::: "memory")
    #define HYPERAMP_ISB()   __asm__ volatile("" ::: "memory")
    
    static inline void hyperamp_cache_clean(volatile void *addr, size_t size) {
        (void)addr; (void)size;
        __asm__ volatile("mfence" ::: "memory");
    }
    
    static inline void hyperamp_cache_invalidate(volatile void *addr, size_t size) {
        (void)addr; (void)size;
        __asm__ volatile("mfence" ::: "memory");
    }
#endif

#if defined(CONFIG_PLAT_IMX8MP_EVK) || defined(CONFIG_PLAT_RK3588)
    // Shared memory configuration for i.MX8MP platform
    #define SHM_TX_QUEUE_PADDR  0x7E000000UL
    #define SHM_RX_QUEUE_PADDR  0x7E001000UL
    #define SHM_DATA_PADDR      0x7E002000UL

#elif defined(CONFIG_PLAT_PHYTIUM_PI)
    // Shared memory configuration for Phytium-Pi platform
    #define SHM_TX_QUEUE_PADDR  0xDE000000UL
    #define SHM_RX_QUEUE_PADDR  0xDE001000UL
    #define SHM_DATA_PADDR      0xDE002000UL

#else
    #error "Unknown Platform! Please define addresses for this board."
#endif


// Virtual address: Start of the TX queue in Hyperamp shared memory (seL4 → Linux)
// #define SHM_TX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x54E000UL)
#define SHM_TX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x55E000UL)


// Virtual address: Start of the RX queue in Hyperamp shared memory (Linux → seL4)
// #define SHM_RX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x54F000UL)
#define SHM_RX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x55F000UL)


// Virtual address: Start of the general data region in Hyperamp shared memory
// #define SHM_DATA_REGION_VA    ((volatile void *)0x550000UL)
#define SHM_DATA_REGION_VA    ((volatile void *)0x560000UL)


//g_tx_queue = (volatile HyperampShmQueue *)0x54e000;
//g_rx_queue = (volatile HyperampShmQueue *)0x54f000;
//g_data_region = (volatile void *)0x550000;


/* HYPERAMP_BARRIER 仅使用 DMB：DSB 已由 LDXR/STLXR 内置语义取代 */
#define HYPERAMP_BARRIER()   HYPERAMP_DMB()

/* ==================== Software Spinlock ==================== */

typedef struct {
    volatile uint32_t lock_value;
    volatile uint32_t owner_zone_id;
    volatile uint32_t lock_count;
    volatile uint32_t contention_count;
} __attribute__((aligned(4))) HyperampSpinlock;
/* 注：移除 __packed__，保留 __aligned(4)。
 *   lock_value 必须 4 字节对齐以支持 LDAXR/STXR。4 个 uint32_t 自然对齐，sizeof = 16B。
 */
/* ==================== Address Mapping Table Entry ==================== */

typedef struct {
    uint64_t virt_addr;
    uint64_t phy_addr;
} __attribute__((packed)) HyperampMapTableEntry;

/* ==================== Shared Memory Pool Queue ==================== */

typedef struct {
    uint8_t  map_mode1;
    uint8_t  map_mode2;
    uint16_t header;
    uint16_t tail;
    uint16_t capacity;
    uint16_t block_size;
    uint16_t _reserved;
    
    uint64_t phy_addr;
    uint64_t virt_addr1;
    uint64_t virt_addr2;
    
    HyperampMapTableEntry table1[HYPERAMP_MAX_MAP_TABLE_ENTRIES];
    HyperampMapTableEntry table2[HYPERAMP_MAX_MAP_TABLE_ENTRIES];
    
    HyperampSpinlock queue_lock;
    
    uint32_t magic;
    uint32_t version;
    uint32_t enqueue_count;
    uint32_t dequeue_count;
    
} __attribute__((packed)) HyperampShmQueue;

/* ==================== Message Header Structure ==================== */

typedef struct {
    uint8_t  version;
    uint8_t  proxy_msg_type;
    uint16_t frontend_sess_id;
    uint16_t backend_sess_id;
    uint16_t payload_len;
} __attribute__((packed)) HyperampMsgHeader;

/* Message types */
typedef enum {
    HYPERAMP_MSG_TYPE_DEV = 0,
    HYPERAMP_MSG_TYPE_STRGY = 1,
    HYPERAMP_MSG_TYPE_SESS = 2,
    HYPERAMP_MSG_TYPE_DATA = 3,
    HYPERAMP_MSG_TYPE_SERVICE = 0x10,
    HYPERAMP_MSG_TYPE_BULK    = 0x20  // Bulk data transfer (payload is a descriptor)
} HyperampMsgType;

// Bulk Transfer configuration
#define BULK_BUFFER_OFFSET        0x100000 // Offset at 1MB
#define BULK_BUFFER_SIZE          (2 * 1024 * 1024) // 2MB buffer size

// Bulk Transfer descriptor (transmitted as payload)
typedef struct {
    uint32_t offset;      // Data offset
    uint32_t length;      // Data length
    uint32_t service_id;  // Service ID (1=Encrypt, 2=Decrypt)
    int32_t status;       // 0=Request, 1=Success, <0=Error
} HyperampBulkDescriptor;

// ==================== Signature Verification ====================

// Service IDs
#define SERVICE_ECHO              0
#define SERVICE_ENCRYPT           1
#define SERVICE_DECRYPT           2
#define SERVICE_VERIFY_ONLY       3   // Signature verification only
#define SERVICE_VERIFY_ENCRYPT    4   // Verify signature then encrypt
#define SERVICE_VERIFY_DECRYPT    5   // Verify signature then decrypt
#define SERVICE_VALIDATE_ENCRYPT  7   // Validate fields then encrypt (for object detection data)
#define SERVICE_VALIDATE_DECRYPT  8   // Validate fields then decrypt (for object detection data)

// Field validation status codes
#define VALIDATE_OK               0
#define VALIDATE_FAILED_MISSING  -10  // Required field missing

// Signature verification status codes
#define AUTH_OK                   0
#define AUTH_FAILED_BAD_MAGIC    -1
#define AUTH_FAILED_BAD_SIG      -2
#define AUTH_FAILED_BAD_LEN      -3

// Signature header magic
#define SIG_MAGIC                 0x53494731  // "SIG1"

// Simplified signature header (for prototype verification)
typedef struct {
    uint32_t magic;           // Must be SIG_MAGIC (0x53494731)
    uint16_t sig_len;         // Signature length (ECDSA: 70–72 bytes)
    uint16_t reserved;
    uint32_t payload_len;     // Original data length
    uint8_t  signature[72];   // ECDSA-P256 signature (max 72 bytes)
} __attribute__((packed)) HyperampSignedHeader;



/* ==================== Queue Configuration Structure ==================== */

/**
 * @brief Queue initialization configuration
 */
typedef struct {
    uint16_t map_mode;      // Memory mapping mode
    uint16_t capacity;      // Queue capacity
    uint16_t block_size;    // Block size
    uint16_t _reserved;
    uint64_t phy_addr;      // Physical address
    uint64_t virt_addr;     // Virtual address
} HyperampQueueConfig;


int hyperamp_queue_is_initialized(volatile HyperampShmQueue *queue);



int hyperamp_queue_init(volatile HyperampShmQueue *queue, 
                        const HyperampQueueConfig *config,
                        int is_creator);


int hyperamp_queue_enqueue(volatile HyperampShmQueue *queue,
                           uint32_t zone_id,
                           const void *data,
                           size_t data_len,
                           volatile void *virt_base);


int hyperamp_queue_dequeue(volatile HyperampShmQueue *queue,
                           uint32_t zone_id,
                           void *data,
                           size_t max_len,
                           size_t *actual_len,
                           volatile void *virt_base);




#endif /* HYPERAMP_SHM_QUEUE_SEL4_H */