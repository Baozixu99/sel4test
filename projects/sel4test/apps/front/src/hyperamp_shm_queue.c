#include "hyperamp_shm_queue.h"
#include "common_utils.h"

// Trusted public key (user-generated ECDSA-P256 public key in DER format)
static const unsigned char TRUSTED_PUBKEY_DER[] = {
  0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
  0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
  0x42, 0x00, 0x04, 0x1b, 0x2e, 0xcc, 0x7e, 0x35, 0xd0, 0xbe, 0xda, 0x02,
  0xce, 0x77, 0x25, 0xf0, 0xbf, 0xa6, 0x87, 0x4f, 0x40, 0xa4, 0xe4, 0xff,
  0xaf, 0xee, 0xb4, 0x5d, 0x12, 0xee, 0x5c, 0x4a, 0x87, 0x07, 0xfe, 0x07,
  0xbf, 0x40, 0xce, 0xb0, 0xb4, 0xa7, 0xcc, 0x7d, 0x7a, 0x85, 0xaf, 0xd3,
  0x23, 0x8d, 0x16, 0xf1, 0x8c, 0x1a, 0x89, 0xca, 0x0c, 0x79, 0x07, 0x43,
  0xca, 0xb9, 0x28, 0xf1, 0xfb, 0xfb, 0x43
};
static const unsigned int TRUSTED_PUBKEY_DER_LEN = 91;



static inline void hyperamp_spinlock_init(volatile HyperampSpinlock *lock)
{
    if (!lock) return;
    lock->lock_value       = 0;
    lock->owner_zone_id    = 0;
    lock->lock_count       = 0;
    lock->contention_count = 0;
    HYPERAMP_BARRIER();
}

static inline void hyperamp_spinlock_lock(volatile HyperampSpinlock *lock, uint32_t zone_id)
{
    if (!lock) return;
#if defined(__aarch64__)
    uint32_t tmp, newval;
    __asm__ volatile(
        "1: ldaxr   %w0, %2\n"
        "   cbnz    %w0, 1b\n"
        "   mov     %w1, #1\n"
        "   stlxr   %w0, %w1, %2\n"
        "   cbnz    %w0, 1b\n"
        : "=&r" (tmp), "=&r" (newval), "+Q" (lock->lock_value)
        :
        : "memory"
    );
    lock->owner_zone_id = zone_id;
    lock->lock_count++;
#else
    while (1) {
        HYPERAMP_BARRIER();
        if (lock->lock_value == 0) {
            lock->lock_value = 1;
            HYPERAMP_BARRIER();
            if (lock->lock_value == 1) {
                lock->owner_zone_id = zone_id;
                lock->lock_count++;
                return;
            }
        }
        lock->contention_count++;
        __asm__ volatile("pause" ::: "memory");
    }
#endif
}

static inline void hyperamp_spinlock_unlock(volatile HyperampSpinlock *lock)
{
    if (!lock) return;
    lock->owner_zone_id = 0;
#if defined(__aarch64__)
    __asm__ volatile(
        "stlr    wzr, %0\n"
        : "+Q" (lock->lock_value)
        :
        : "memory"
    );
#else
    HYPERAMP_BARRIER();
    lock->lock_value = 0;
    HYPERAMP_BARRIER();
#endif
}

/* 验证 queue_lock.lock_value 在内存中满足 LDAXR/STXR 需要的 4 字节对齐 */
_Static_assert(offsetof(HyperampShmQueue, queue_lock) % 4 == 0,
               "HyperampShmQueue.queue_lock must be 4-byte aligned for LDAXR/STXR");
_Static_assert(sizeof(HyperampSpinlock) == 16,
               "HyperampSpinlock size must be 16 bytes");

/* 安全获取 packed 结构体中 queue_lock 的指针（避免 -Waddress-of-packed-member） */
#define HYPERAMP_QUEUE_LOCK(q) \
    ((volatile HyperampSpinlock *)((volatile uint8_t *)(q) + offsetof(HyperampShmQueue, queue_lock)))
/* ==================== Secure Memory Operations ==================== */

static inline void hyperamp_safe_memset(volatile void *dst, uint8_t val, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0; i < len; i++) {
        p[i] = val;
    }
    HYPERAMP_BARRIER();
}

static inline void hyperamp_safe_memcpy(volatile void *dst, const volatile void *src, size_t len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    HYPERAMP_BARRIER();
}

uint16_t hyperamp_safe_read_u16(const volatile void *addr, size_t offset)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint16_t val = 0;
    for (int i = 0; i < 2; i++) {
        val |= ((uint16_t)p[offset + i]) << (i * 8);
    }
    HYPERAMP_BARRIER();
    return val;
}

static inline uint32_t hyperamp_safe_read_u32(const volatile void *addr, size_t offset)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        val |= ((uint32_t)p[offset + i]) << (i * 8);
    }
    HYPERAMP_BARRIER();
    return val;
}

static inline uint64_t hyperamp_safe_read_u64(const volatile void *addr, size_t offset)
{
    const volatile uint8_t *p = (const volatile uint8_t *)addr;
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)p[offset + i]) << (i * 8);
    }
    HYPERAMP_BARRIER();
    return val;
}

/* ==================== Queue Operation Functions (Minimal Version, No printf) ==================== */



/**
 * @brief Check whether the queue has been initialized
 */
int hyperamp_queue_is_initialized(volatile HyperampShmQueue *queue)
{
    if (!queue) return 0;
    
    HYPERAMP_BARRIER();
    
    // Avoid using magic field (offset 4052 > 4096, crosses page boundary);
    // instead use capacity field (offset 6)
    size_t capacity_offset = offsetof(HyperampShmQueue, capacity);
    volatile uint8_t *p = (volatile uint8_t *)queue;
    
    uint16_t capacity = 0;
    for (int i = 0; i < 2; i++) {
        capacity |= ((uint16_t)p[capacity_offset + i]) << (i * 8);
    }
    
    HYPERAMP_BARRIER();
    // Initialization indicator: capacity > 0
    return (capacity > 0);
}

/**
 * @brief Enqueue data into the shared memory queue
 * @param queue [in/out] Pointer to the shared memory queue control structure (header and enqueue_count will be updated internally)
 * @param zone_id [in] Caller's zone ID (used for acquiring the spinlock)
 * @param data [in] Source data buffer containing the data to write
 * @param data_len [in] Number of bytes to write (must be <= queue block_size)
 * @param virt_base [in] Base virtual address of the shared memory data region (used to compute write offset)
 * @return HYPERAMP_OK on success, HYPERAMP_ERROR on failure (invalid args, oversized data, or full queue)
 */
int hyperamp_queue_enqueue(volatile HyperampShmQueue *queue,
                          uint32_t zone_id,
                          const void *data,
                          size_t data_len,
                          volatile void *virt_base)
{
    if (!queue || !data || data_len == 0) {
        printf("hyperamp_queue_enqueue failed: queue =%p, data = %p, data_len = %d\n", queue, data, data_len);
        return HYPERAMP_ERROR;
    }
    
    // Safely read block_size and capacity
    uint16_t block_size = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, block_size));
    uint16_t capacity = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, capacity));
    
    if (data_len > block_size) return HYPERAMP_ERROR;
    
    // Acquire the lock
     hyperamp_spinlock_lock(HYPERAMP_QUEUE_LOCK(queue), zone_id);
    
    // Safely read header and tail
    uint16_t header = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, header));
    uint16_t tail = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, tail));
    
    // Calculate the new header index
    printf("In %s, tail = %u, header = %u\n", __func__, tail, header);
    uint16_t new_header = header + 1;
    if (new_header >= capacity) {
        new_header -= capacity;
    }
    
    parse_proxy_protocol_and_print(data);

    // Check if the queue is full
    if (new_header == tail) {
        hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));
        return HYPERAMP_AGAIN;
    }
    
    // Compute the write address
    uint64_t write_addr = (uint64_t)virt_base + (uint64_t)(header + 1) * block_size;
    
    // Write the data
    hyperamp_safe_memcpy((volatile void *)write_addr, data, data_len);
    
    // Update the header (byte-by-byte write for atomicity)
    volatile uint8_t *p = (volatile uint8_t *)queue;
    size_t header_offset = offsetof(HyperampShmQueue, header);
    p[header_offset] = new_header & 0xFF;
    p[header_offset + 1] = (new_header >> 8) & 0xFF;
    
    // Update enqueue_count (byte-by-byte write)
    size_t enqueue_offset = offsetof(HyperampShmQueue, enqueue_count);
    uint32_t enqueue_count = hyperamp_safe_read_u32(queue, enqueue_offset);
    enqueue_count++;
    for (int i = 0; i < 4; i++) {
        p[enqueue_offset + i] = (enqueue_count >> (i * 8)) & 0xFF;
    }
    
    HYPERAMP_BARRIER();
    
    /* Flush written data to memory */
    hyperamp_cache_clean((volatile void *)write_addr, data_len);
    /* Flush the queue control block to memory */
    hyperamp_cache_clean((volatile void *)queue, 64); /* Only flush the first 64 bytes containing control fields */
    
    // Release the lock
    hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));
    
    return HYPERAMP_OK;
}


//helper function
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


/**
 * @brief Dequeue data from the shared memory queue
 * @param queue [in/out] Pointer to the shared memory queue control structure (tail and dequeue_count will be updated internally)
 * @param zone_id [in] Caller's zone ID (used for acquiring the spinlock)
 * @param data [out] Destination buffer to store dequeued data
 * @param max_len [in] Maximum capacity of the destination buffer (to prevent overflow)
 * @param actual_len [out] Actual number of bytes read (can be NULL if not needed)
 * @param virt_base [in] Base virtual address of the shared memory data region (used to compute read offset)
 * @return HYPERAMP_OK on success, HYPERAMP_ERROR on failure (invalid args or empty queue)
 */
int hyperamp_queue_dequeue(volatile HyperampShmQueue *queue,
                          uint32_t zone_id,
                          void *data,
                          size_t max_len,
                          size_t *actual_len,
                          volatile void *virt_base)
{
    if (!queue || !data || max_len == 0) {
        printf("hyperamp_queue_dequeue failed: queue =%p, data = %p, max_len = %d\n", queue, data, max_len);    
        return HYPERAMP_ERROR;
    }
    
    /* Invalidate cache before reading to ensure the latest data is fetched */
    hyperamp_cache_invalidate((volatile void *)queue, 64);
    HYPERAMP_BARRIER();
    
    // Acquire the lock
    hyperamp_spinlock_lock(HYPERAMP_QUEUE_LOCK(queue), zone_id);
    
    // Safely read header, tail, block_size, and capacity
    uint16_t header = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, header));
    uint16_t tail = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, tail));
    uint16_t block_size = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, block_size));
    uint16_t capacity = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, capacity));
    
    // Check if the queue is empty
    printf("In %s, tail = %u, header = %u\n", __func__, tail, header);
    if (tail == header) {
        hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));
        return HYPERAMP_AGAIN;
    }
    
    // Compute the read address
    uint64_t read_addr = (uint64_t)virt_base + (uint64_t)(tail + 1) * block_size;
    
    /* Invalidate the data region cache to ensure the latest data is read */
    hyperamp_cache_invalidate((volatile void *)read_addr, block_size);
    HYPERAMP_BARRIER();
    
    // Determine the actual number of bytes to read
    size_t read_len = (max_len < block_size) ? max_len : block_size;
    
    // Read the data
    hyperamp_safe_memcpy(data, (const volatile void *)read_addr, read_len);

    parse_proxy_protocol_and_print(data);

    print_hex(data, read_len, 64);
    print_hex((void*) read_addr, read_len, 64);

#if 1
    printf("In %s, before update the tail pointer and the dequeue count, the content of message header:\n", __func__);
//    DUMP_PROXY_MSG_HEADER(data);
#endif


    if (actual_len) {
        *actual_len = read_len;
    }
    
//    printf("After set actual_len\n");
//    DUMP_PROXY_MSG_HEADER(data);
    // Update tail (byte-by-byte write for atomicity)
    uint16_t new_tail = tail + 1;
    if (new_tail >= capacity) {
        new_tail -= capacity;
    }

    printf("After set new_tail\n");
    DUMP_PROXY_MSG_HEADER(data);

#if 1    
    volatile uint8_t *p = (volatile uint8_t *)queue;
    size_t tail_offset = offsetof(HyperampShmQueue, tail);
    p[tail_offset] = new_tail & 0xFF;
    p[tail_offset + 1] = (new_tail >> 8) & 0xFF;
    
    // Update dequeue_count (byte-by-byte write)
    size_t dequeue_offset = offsetof(HyperampShmQueue, dequeue_count);
    uint32_t dequeue_count = hyperamp_safe_read_u32(queue, dequeue_offset);
    dequeue_count++;
    for (int i = 0; i < 4; i++) {
        p[dequeue_offset + i] = (dequeue_count >> (i * 8)) & 0xFF;
    }
#endif


 #if 1
    printf("In %s, after update the tail pointer and the dequeue count, the content of message header:\n", __func__);
    DUMP_PROXY_MSG_HEADER(data);
#endif

    HYPERAMP_BARRIER();
    
    // Release the lock
    hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));

    printf("After unlock spinlock\n");
    DUMP_PROXY_MSG_HEADER(data);
    
    return HYPERAMP_OK;
}

/**
 * @brief Initialize the shared memory queue (seL4 can now initialize queues itself!)
 * @param queue Pointer to the queue
 * @param config Configuration parameters
 * @param is_creator Whether this process is the queue creator (creator initializes all fields)
 * @return HYPERAMP_OK on success, HYPERAMP_ERROR on failure
 */
int hyperamp_queue_init(volatile HyperampShmQueue *queue, 
                        const HyperampQueueConfig *config,
                        int is_creator)
{
    if (!queue || !config) return HYPERAMP_ERROR;
    if (config->block_size == 0 || config->capacity == 0) return HYPERAMP_ERROR;
    
    if (is_creator) {
        // Creator: use safe byte-by-byte writes
        volatile uint8_t *p = (volatile uint8_t *)queue;
        
        // Write map_mode1 and map_mode2
        p[0] = config->map_mode;
        p[1] = config->map_mode;
        HYPERAMP_BARRIER();
        
        // Write header (uint16_t, offset 2)
        p[2] = 0;
        p[3] = 0;
        HYPERAMP_BARRIER();
        
        // Write tail (uint16_t, offset 4)
        p[4] = 0;
        p[5] = 0;
        HYPERAMP_BARRIER();
        
        // Write capacity (uint16_t, offset 6)
        uint16_t cap = config->capacity;
        p[6] = cap & 0xFF;
        p[7] = (cap >> 8) & 0xFF;
        HYPERAMP_BARRIER();
        
        // Write block_size (uint16_t, offset 8)
        uint16_t bs = config->block_size;
        p[8] = bs & 0xFF;
        p[9] = (bs >> 8) & 0xFF;
        HYPERAMP_BARRIER();
        
        // Write _reserved (uint16_t, offset 10)
        p[10] = 0;
        p[11] = 0;
        HYPERAMP_BARRIER();
        
        // Write phy_addr (uint64_t, offset 12) – byte-by-byte
        uint64_t pa = config->phy_addr;
        for (int i = 0; i < 8; i++) {
            p[12 + i] = (pa >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();
        
        // Write virt_addr1 (uint64_t, offset 20)
        uint64_t va = config->virt_addr;
        for (int i = 0; i < 8; i++) {
            p[20 + i] = (va >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();
        
        // Write virt_addr2 (uint64_t, offset 28) – zero out
        for (int i = 0; i < 8; i++) {
            p[28 + i] = 0;
        }
        HYPERAMP_BARRIER();
        
        // Initialize spinlock
        size_t lock_offset = offsetof(HyperampShmQueue, queue_lock);
        volatile HyperampSpinlock *lock = (volatile HyperampSpinlock *)&p[lock_offset];
        hyperamp_spinlock_init(lock);
        
        // Write magic (uint32_t)
        size_t magic_offset = offsetof(HyperampShmQueue, magic);
        uint32_t magic = HYPERAMP_QUEUE_MAGIC;
        for (int i = 0; i < 4; i++) {
            p[magic_offset + i] = (magic >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();
        
        // Write version (uint32_t)
        uint32_t version = 1;
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 4 + i] = (version >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();
        
        // Write enqueue_count (uint32_t) – zero out
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 8 + i] = 0;
        }
        HYPERAMP_BARRIER();
        
        // Write dequeue_count (uint32_t) – zero out
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 12 + i] = 0;
        }
        HYPERAMP_BARRIER();
        
        /* Critical: flush entire queue structure to memory,
           ensuring other CPUs/Zones can observe the initialization */
        hyperamp_cache_clean((volatile void *)queue, sizeof(HyperampShmQueue));
        
    } else {
        // Non-creator: only set own virtual address
        queue->virt_addr2 = config->virt_addr;
        HYPERAMP_BARRIER();
    }
    
    return HYPERAMP_OK;
}