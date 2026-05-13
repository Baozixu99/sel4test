/**
 * @file hyperamp_shm_queue.c
 * @brief HyperAMP Shared Memory Queue – monkey-mnemosyne local copy
 *
 * Copied from apps/front/src/hyperamp_shm_queue.c and cleaned up:
 *
 *   1. Removed #include "common_utils.h" and all calls to
 *      parse_proxy_protocol_and_print() / DUMP_PROXY_MSG_HEADER().
 *   2. Removed print_hex() / print_string() debug helpers (not needed).
 *   3. Fixed the original bug on line 343 where print_hex() was called with
 *      a uint64_t instead of a const uint8_t*.
 *   4. Removed the TRUSTED_PUBKEY_DER array (signature verification is
 *      not used by monkey-mnemosyne).
 *   5. Retained all queue operation logic verbatim.
 */

#include "hyperamp_shm_queue.h"


/* ==================== Software Spinlock ==================== */

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


/* Verify queue_lock alignment for LDAXR/STXR on aarch64. */
_Static_assert(offsetof(HyperampShmQueue, queue_lock) % 4 == 0,
               "HyperampShmQueue.queue_lock must be 4-byte aligned for LDAXR/STXR");
_Static_assert(sizeof(HyperampSpinlock) == 16,
               "HyperampSpinlock size must be 16 bytes");

/* Safe pointer to queue_lock (avoids -Waddress-of-packed-member). */
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


/* ==================== Queue Operation Functions ==================== */

int hyperamp_queue_is_initialized(volatile HyperampShmQueue *queue)
{
    if (!queue) return 0;

    HYPERAMP_BARRIER();

    size_t capacity_offset = offsetof(HyperampShmQueue, capacity);
    volatile uint8_t *p = (volatile uint8_t *)queue;

    uint16_t capacity = 0;
    for (int i = 0; i < 2; i++) {
        capacity |= ((uint16_t)p[capacity_offset + i]) << (i * 8);
    }

    HYPERAMP_BARRIER();
    return (capacity > 0);
}


int hyperamp_queue_enqueue(volatile HyperampShmQueue *queue,
                           uint32_t zone_id,
                           const void *data,
                           size_t data_len,
                           volatile void *virt_base)
{
    if (!queue || !data || data_len == 0) {
        printf("hyperamp_queue_enqueue failed: queue=%p, data=%p, data_len=%zu\n",
               (void *)queue, data, data_len);
        return HYPERAMP_ERROR;
    }

    uint16_t block_size = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, block_size));
    uint16_t capacity   = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, capacity));

    if (data_len > block_size) return HYPERAMP_ERROR;

    hyperamp_spinlock_lock(HYPERAMP_QUEUE_LOCK(queue), zone_id);

    uint16_t header = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, header));
    uint16_t tail   = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, tail));

    uint16_t new_header = header + 1;
    if (new_header >= capacity) {
        new_header -= capacity;
    }

    /* Check if the queue is full. */
    if (new_header == tail) {
        hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));
        return HYPERAMP_AGAIN;
    }

    /* Compute the write address. */
    uint64_t write_addr = (uint64_t)(uintptr_t)virt_base + (uint64_t)(header + 1) * block_size;

    /* Write the data. */
    hyperamp_safe_memcpy((volatile void *)(uintptr_t)write_addr, data, data_len);

    /* Update the header (byte-by-byte). */
    volatile uint8_t *p = (volatile uint8_t *)queue;
    size_t header_offset = offsetof(HyperampShmQueue, header);
    p[header_offset]     = new_header & 0xFF;
    p[header_offset + 1] = (new_header >> 8) & 0xFF;

    /* Update enqueue_count. */
    size_t enqueue_offset = offsetof(HyperampShmQueue, enqueue_count);
    uint32_t enqueue_count = hyperamp_safe_read_u32(queue, enqueue_offset);
    enqueue_count++;
    for (int i = 0; i < 4; i++) {
        p[enqueue_offset + i] = (enqueue_count >> (i * 8)) & 0xFF;
    }

    HYPERAMP_BARRIER();

    hyperamp_cache_clean((volatile void *)(uintptr_t)write_addr, data_len);
    hyperamp_cache_clean((volatile void *)queue, 64);

    hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));

    return HYPERAMP_OK;
}


int hyperamp_queue_dequeue(volatile HyperampShmQueue *queue,
                           uint32_t zone_id,
                           void *data,
                           size_t max_len,
                           size_t *actual_len,
                           volatile void *virt_base)
{
    if (!queue || !data || max_len == 0) {
        printf("hyperamp_queue_dequeue failed: queue=%p, data=%p, max_len=%zu\n",
               (void *)queue, data, max_len);
        return HYPERAMP_ERROR;
    }

    hyperamp_cache_invalidate((volatile void *)queue, 64);
    HYPERAMP_BARRIER();

    hyperamp_spinlock_lock(HYPERAMP_QUEUE_LOCK(queue), zone_id);

    uint16_t header     = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, header));
    uint16_t tail       = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, tail));
    uint16_t block_size = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, block_size));
    uint16_t capacity   = hyperamp_safe_read_u16(queue, offsetof(HyperampShmQueue, capacity));

    /* Check if the queue is empty. */
    if (tail == header) {
        hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));
        return HYPERAMP_AGAIN;
    }

    /* Compute the read address. */
    uint64_t read_addr = (uint64_t)(uintptr_t)virt_base + (uint64_t)(tail + 1) * block_size;

    hyperamp_cache_invalidate((volatile void *)(uintptr_t)read_addr, block_size);
    HYPERAMP_BARRIER();

    size_t read_len = (max_len < block_size) ? max_len : block_size;

    hyperamp_safe_memcpy(data, (const volatile void *)(uintptr_t)read_addr, read_len);

    if (actual_len) {
        *actual_len = read_len;
    }

    /* Update tail. */
    uint16_t new_tail = tail + 1;
    if (new_tail >= capacity) {
        new_tail -= capacity;
    }

    volatile uint8_t *p = (volatile uint8_t *)queue;
    size_t tail_offset = offsetof(HyperampShmQueue, tail);
    p[tail_offset]     = new_tail & 0xFF;
    p[tail_offset + 1] = (new_tail >> 8) & 0xFF;

    /* Update dequeue_count. */
    size_t dequeue_offset = offsetof(HyperampShmQueue, dequeue_count);
    uint32_t dequeue_count = hyperamp_safe_read_u32(queue, dequeue_offset);
    dequeue_count++;
    for (int i = 0; i < 4; i++) {
        p[dequeue_offset + i] = (dequeue_count >> (i * 8)) & 0xFF;
    }

    HYPERAMP_BARRIER();

    hyperamp_spinlock_unlock(HYPERAMP_QUEUE_LOCK(queue));

    return HYPERAMP_OK;
}


int hyperamp_queue_init(volatile HyperampShmQueue *queue,
                        const HyperampQueueConfig *config,
                        int is_creator)
{
    if (!queue || !config) return HYPERAMP_ERROR;
    if (config->block_size == 0 || config->capacity == 0) return HYPERAMP_ERROR;

    if (is_creator) {
        volatile uint8_t *p = (volatile uint8_t *)queue;

        /* map_mode1, map_mode2 */
        p[0] = config->map_mode;
        p[1] = config->map_mode;
        HYPERAMP_BARRIER();

        /* header (uint16_t, offset 2) */
        p[2] = 0; p[3] = 0;
        HYPERAMP_BARRIER();

        /* tail (uint16_t, offset 4) */
        p[4] = 0; p[5] = 0;
        HYPERAMP_BARRIER();

        /* capacity (uint16_t, offset 6) */
        uint16_t cap = config->capacity;
        p[6] = cap & 0xFF;
        p[7] = (cap >> 8) & 0xFF;
        HYPERAMP_BARRIER();

        /* block_size (uint16_t, offset 8) */
        uint16_t bs = config->block_size;
        p[8] = bs & 0xFF;
        p[9] = (bs >> 8) & 0xFF;
        HYPERAMP_BARRIER();

        /* _reserved (uint16_t, offset 10) */
        p[10] = 0; p[11] = 0;
        HYPERAMP_BARRIER();

        /* phy_addr (uint64_t, offset 12) */
        uint64_t pa = config->phy_addr;
        for (int i = 0; i < 8; i++) {
            p[12 + i] = (pa >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();

        /* virt_addr1 (uint64_t, offset 20) */
        uint64_t va = config->virt_addr;
        for (int i = 0; i < 8; i++) {
            p[20 + i] = (va >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();

        /* virt_addr2 (uint64_t, offset 28) – zero */
        for (int i = 0; i < 8; i++) {
            p[28 + i] = 0;
        }
        HYPERAMP_BARRIER();

        /* Initialize spinlock */
        size_t lock_offset = offsetof(HyperampShmQueue, queue_lock);
        volatile HyperampSpinlock *lock = (volatile HyperampSpinlock *)&p[lock_offset];
        hyperamp_spinlock_init(lock);

        /* magic */
        size_t magic_offset = offsetof(HyperampShmQueue, magic);
        uint32_t magic = HYPERAMP_QUEUE_MAGIC;
        for (int i = 0; i < 4; i++) {
            p[magic_offset + i] = (magic >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();

        /* version */
        uint32_t ver = 1;
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 4 + i] = (ver >> (i * 8)) & 0xFF;
        }
        HYPERAMP_BARRIER();

        /* enqueue_count – zero */
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 8 + i] = 0;
        }
        HYPERAMP_BARRIER();

        /* dequeue_count – zero */
        for (int i = 0; i < 4; i++) {
            p[magic_offset + 12 + i] = 0;
        }
        HYPERAMP_BARRIER();

        hyperamp_cache_clean((volatile void *)queue, sizeof(HyperampShmQueue));

    } else {
        queue->virt_addr2 = config->virt_addr;
        HYPERAMP_BARRIER();
    }

    return HYPERAMP_OK;
}
