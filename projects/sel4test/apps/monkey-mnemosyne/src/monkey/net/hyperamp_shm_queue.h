/**
 * @file hyperamp_shm_queue.h
 * @brief HyperAMP Shared Memory Queue – monkey-mnemosyne local copy
 *
 * Copied from apps/front/include/hyperamp_shm_queue.h and stripped down for
 * use within monkey-mnemosyne.  Changes from the original:
 *
 *   1. Removed #include "hardware_config.h" – platform physical addresses are
 *      guarded by __aarch64__ instead of CONFIG_PLAT_* so that x86_64
 *      simulation builds work.
 *   2. Removed dependency on common_utils.h.
 *
 * Layout compatibility – CRITICAL
 * --------------------------------
 *   `HyperampShmQueue` MUST stay byte-identical to the version in
 *   apps/front/include/hyperamp_shm_queue.h, because both seL4 (this code)
 *   and the Linux side (HighSpeedCProxy / hvisor-tool) point at the same
 *   physical shared-memory pages and read/write the same fields.
 *
 *   The original struct uses `__attribute__((packed))`.  An earlier
 *   revision of this local copy removed `__packed__` to silence
 *   `-Wpacked-not-aligned` (HyperampSpinlock carries `aligned(4)` and the
 *   compiler complains when an aligned member sits inside a packed
 *   container).  That was a layout bug: dropping `__packed__` causes the
 *   compiler to insert 4 bytes of padding before `phy_addr` (uint64 at
 *   natural offset 12 → padded to 16), shifting everything afterwards
 *   (queue_lock, magic, enqueue_count, dequeue_count) by 4 bytes relative
 *   to the Linux side.  Result: the cross-VM spinlock and the magic check
 *   land at different offsets in the two views, breaking interop.
 *
 *   The packed attribute is therefore restored, and the
 *   `-Wpacked-not-aligned` warning is suppressed locally with
 *   `#pragma GCC diagnostic`.  Static asserts at the bottom of this header
 *   pin the critical offsets so that any future regression breaks the
 *   build instead of the wire.
 *
 * All queue function signatures remain binary-compatible with the original.
 */

#ifndef HYPERAMP_SHM_QUEUE_LOCAL_H
#define HYPERAMP_SHM_QUEUE_LOCAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ==================== Constant Definitions ==================== */

#define HYPERAMP_ERROR_ADDR             UINT64_MAX
#define HYPERAMP_MAX_MAP_TABLE_ENTRIES  125

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
#define HYPERAMP_QUEUE_MAGIC            0x48415150  /* "HAQP" */


/* ==================== Memory Barriers and Cache Operations ==================== */

#if defined(__aarch64__) || defined(__arm__)
    #define HYPERAMP_DMB()   __asm__ volatile("dmb sy" ::: "memory")
    #define HYPERAMP_ISB()   __asm__ volatile("isb" ::: "memory")

    static inline void hyperamp_cache_clean(volatile void *addr, size_t size) {
        volatile char *p = (volatile char *)addr;
        volatile char *end = p + size;
        for (; p < end; p += 64) {
            __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
        }
        __asm__ volatile("dsb sy" ::: "memory");
    }

    static inline void hyperamp_cache_invalidate(volatile void *addr, size_t size) {
        volatile char *p = (volatile char *)addr;
        volatile char *end = p + size;
        for (; p < end; p += 64) {
            __asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
        }
        __asm__ volatile("dsb sy" ::: "memory");
    }
#else
    /* x86 / simulation fallback */
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

/* ==================== Platform addresses ==================== */

/*
 * Physical addresses – only meaningful on real hardware.
 * For simulation builds we still need the symbols to exist so that
 * the code compiles; the actual values are irrelevant because the
 * bridge uses the compile-time virtual addresses below.
 */
#if defined(__aarch64__) || defined(__arm__)
    #define SHM_TX_QUEUE_PADDR  0x7E000000UL
    #define SHM_RX_QUEUE_PADDR  0x7E001000UL
    #define SHM_DATA_PADDR      0x7E002000UL
#else
    /* x86_64 simulation – dummy physical addresses (never dereferenced). */
    #define SHM_TX_QUEUE_PADDR  0x7E000000UL
    #define SHM_RX_QUEUE_PADDR  0x7E001000UL
    #define SHM_DATA_PADDR      0x7E002000UL
#endif

/* Virtual addresses – these are the fixed mappings set up by the seL4 boot
 * loader (boot.c).  Identical across platforms.  */
#define SHM_TX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x55E000UL)
#define SHM_RX_QUEUE_VADDR    ((volatile HyperampShmQueue *)0x55F000UL)
#define SHM_DATA_REGION_VA    ((volatile void *)0x560000UL)


#define HYPERAMP_BARRIER()   HYPERAMP_DMB()

/* ==================== Software Spinlock ==================== */

typedef struct {
    volatile uint32_t lock_value;
    volatile uint32_t owner_zone_id;
    volatile uint32_t lock_count;
    volatile uint32_t contention_count;
} __attribute__((aligned(4))) HyperampSpinlock;

/* ==================== Address Mapping Table Entry ==================== */

typedef struct {
    uint64_t virt_addr;
    uint64_t phy_addr;
} __attribute__((packed)) HyperampMapTableEntry;

/* ==================== Shared Memory Pool Queue ==================== */

/*
 * HyperampShmQueue MUST be __packed__ to keep its layout byte-identical to
 * apps/front/include/hyperamp_shm_queue.h (and therefore to whatever the
 * Linux side – HighSpeedCProxy / hvisor-tool – sees on the same physical
 * pages).  See the layout-compatibility note at the top of this file for
 * why an earlier revision that dropped `packed` was wrong.
 *
 * GCC emits -Wpacked-not-aligned because HyperampSpinlock carries
 * `aligned(4)` and lives inside a packed container.  That warning is real
 * but harmless in this layout (the spinlock falls on a 4-byte boundary by
 * construction – see the static asserts below) so we suppress it locally
 * rather than restructure the struct.  The pragma is GCC-only because
 * -Wpacked-not-aligned is a GCC-specific flag; clang doesn't recognise
 * the warning name and would itself warn on the pragma, so we guard.
 */
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpacked-not-aligned"
#endif
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
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

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
    HYPERAMP_MSG_TYPE_DEV     = 0,
    HYPERAMP_MSG_TYPE_STRGY   = 1,
    HYPERAMP_MSG_TYPE_SESS    = 2,
    HYPERAMP_MSG_TYPE_DATA    = 3,
    HYPERAMP_MSG_TYPE_SERVICE = 0x10,
    HYPERAMP_MSG_TYPE_BULK    = 0x20
} HyperampMsgType;

/* ==================== Queue Configuration Structure ==================== */

typedef struct {
    uint16_t map_mode;
    uint16_t capacity;
    uint16_t block_size;
    uint16_t _reserved;
    uint64_t phy_addr;
    uint64_t virt_addr;
} HyperampQueueConfig;


/* ==================== Function Declarations ==================== */

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


/* ==================== Layout invariants ==================== */
/*
 * Pin the cross-VM byte layout so that any future regression (e.g. a
 * well-meaning soul deleting `__packed__` again to silence a warning)
 * breaks the build instead of breaking the wire.
 *
 * These offsets are the values that apps/front/include/hyperamp_shm_queue.h
 * (= the Linux side via HighSpeedCProxy / hvisor-tool) sees, computed for
 * the canonical packed struct.
 */
#ifdef __cplusplus
#define MNEMOSYNE_STATIC_ASSERT(expr, msg) static_assert(expr, msg)
#else
#define MNEMOSYNE_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#endif

MNEMOSYNE_STATIC_ASSERT(sizeof(HyperampShmQueue) == 4068,
    "HyperampShmQueue size drifted from the cross-VM contract (4068 B). "
    "Did someone remove __attribute__((packed))?");

MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, header) == 2,
    "HyperampShmQueue.header offset drifted (must be 2)");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, tail) == 4,
    "HyperampShmQueue.tail offset drifted (must be 4)");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, capacity) == 6,
    "HyperampShmQueue.capacity offset drifted (must be 6)");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, block_size) == 8,
    "HyperampShmQueue.block_size offset drifted (must be 8)");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, phy_addr) == 12,
    "HyperampShmQueue.phy_addr offset drifted (must be 12). "
    "If this fires, the struct is no longer packed and 4 bytes of "
    "padding got inserted before phy_addr, breaking cross-VM interop.");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, queue_lock) == 4036,
    "HyperampShmQueue.queue_lock offset drifted (must be 4036). "
    "Cross-VM spinlock will silently desynchronise.");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, queue_lock) % 4 == 0,
    "HyperampShmQueue.queue_lock must be 4-byte aligned for LDAXR/STXR");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, magic) == 4052,
    "HyperampShmQueue.magic offset drifted (must be 4052). "
    "Connector-side magic check will see garbage.");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, enqueue_count) == 4060,
    "HyperampShmQueue.enqueue_count offset drifted (must be 4060)");
MNEMOSYNE_STATIC_ASSERT(offsetof(HyperampShmQueue, dequeue_count) == 4064,
    "HyperampShmQueue.dequeue_count offset drifted (must be 4064)");


#endif /* HYPERAMP_SHM_QUEUE_LOCAL_H */
