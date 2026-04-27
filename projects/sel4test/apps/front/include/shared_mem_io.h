#ifndef SHARED_MEM_IO_H
#define SHARED_MEM_IO_H


#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "session.h"
#include "hyperamp_shm_queue.h"

#define ERROR_SHARED_MEM_ADDR           UINT64_MAX

#define MAX_MAP_TABLE_ENTRY_COUNT       64

#define HSNET_RX_PHY_ADDR_BASE          0xA000
#define HSNET_MEM_BLOCK_SIZE            4096
#define HSNET_RX_MEM_BLOCK_COUNT        MAX_MAP_TABLE_ENTRY_COUNT

#define HSNET_TX_PHY_ADDR_BASE          HSNET_RX_PHY_ADDR_BASE + HSNET_MEM_BLOCK_SIZE * MAX_MAP_TABLE_ENTRY_COUNT




struct SharedMemoryPoolLock{
    int value; // Just for placehoder. We will redefine this struct after receiving the partner's document.
};

struct SharedMemoryPool{
    int                             value; // Just for placehoder. We will redefine this struct after receiving the partner's document.
    struct SharedMemoryPoolLock     lock;
};

struct DequeNode{
    int                             value; // // Just for placehoder. We will redefine this struct after receiving the partner's document.
    struct DequeNode                *prev;
    struct DequeNode                *next;
};

/**
 * Enumeration of shared memory mapping modes, describing the continuity relationship between physical and logical addresses
 */
typedef enum {
    SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH,                  // Physical addresses are contiguous, logical addresses are contiguous
    SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL  // Physical addresses are contiguous, logical addresses are discrete
} ShareMemMapMode;



/**
 * When the map mode is SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL, the shared memory queue maintains a mapping table.
 * This table contains a set of MapTableEntries to establish the mapping relationship between physical addresses and virtual addresses.
 * 
 * Mapping table entry: Describes a single mapping relationship between a virtual address and a physical address.
 */
typedef struct {
    uint64_t    virt_addr;  /* Virtual address */
    uint64_t    phy_addr;   /* Physical address */
} MapTableEntry;





/**
 * @brief FIFO queue (ring buffer implementation) based on SharedMemoryPool
 * A high-efficiency first-in-first-out (FIFO) queue implemented with a ring buffer structure,
 * which allocates memory from an associated shared memory pool. It is designed for inter-process
 * or inter-thread communication, managing element enqueue/dequeue via ring buffer indexes
 * and reflecting memory allocation details through core parameters.
 */
struct SharedMemoryPoolQueue {
    /* 
     * the memory-maping mode in Linux side. 
     */
    uint8_t                map_mode1;
    /* 
     * the memory-maping mode in microkernel-OS side. 
     */
    uint8_t                map_mode2;

    /* header index of the ring buffer, pointing to the next free slot for enqueuing.
       Works with `tail` to maintain FIFO order. */
    uint16_t                header;

    /* tail index of the ring buffer, pointing to the next element to be dequeued.
       Works with `header` to maintain FIFO order. */
    uint16_t                tail;

    /* Current number of elements in the queue. Dynamically updated with
       enqueue (increment) and dequeue (decrement) operations. */
 //   size_t                  length;

    /* Total memory size (in bytes) allocated to this queue from the shared memory pool.
       Determines the maximum storage space available for elements. */
    /* 
     * Maximum number of elements the queue can hold, representing the upper limit of elements 
     * based on allocated memory and element size. 
     */
    uint16_t                  capacity;

    /* Maximum number of elements the queue can hold. Calculated as (capacity / block_size),
       representing the upper limit of elements based on allocated memory and element size. */
//    size_t                  max_num_items;

    /* Size (in bytes) of each element's memory block. All elements in the queue
       occupy a fixed size to simplify memory management and access. */
    uint16_t                  block_size;
    /*
     * Physical address of the queue control block in shared memory.
     * Used for direct access across processes or between kernel and user spaces.
     * Can also assist in spinlock/mutex synchronization during sharing processes.
     */
    uint64_t                phy_addr;
    /* 
     * Virtual address of the queue control block in the Linux size.
     * Valid when map_mode1 is SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH.
     */
    uint64_t                virt_addr1;
    /* 
     * Virtual address of the queue control block in the microkernel-OS size.
     * Valid when map_mode2 is SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH
     */
    uint64_t                virt_addr2;
    /*
     * Address mapping table for the Linux side, storing up to MAX_MAP_TABLE_ENTRY_COUNT entries
     * that map virtual addresses to physical addresses.
     * 
     * * Valid when map_mode1 is SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL.
     */
    MapTableEntry           table1[MAX_MAP_TABLE_ENTRY_COUNT];
    /*
     * Address mapping table for the microkernel-OS side, storing up to MAX_MAP_TABLE_ENTRY_COUNT entries
     * that map virtual addresses to physical addresses.
     * 
     * Valid when map_mode2 is SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL.
     */
    MapTableEntry           table2[MAX_MAP_TABLE_ENTRY_COUNT];
    /* 
     * Pointer to the associated shared memory pool. All memory for the queue
     * (including control block and data blocks) is allocated from this pool. 
     */
    struct SharedMemoryPool *pool;
}__attribute__((packed));


/**
 * @brief Configuration parameters structure for initializing SharedMemoryPoolQueue
 * 
 * A structure containing all necessary parameters required to initialize a SharedMemoryPoolQueue
 * instance. It aggregates external input parameters that define the queue's memory properties
 * and association with a shared memory pool, serving as a clean interface for queue initialization.
 * 
 * Key parameters include:
 * - `pool`: Reference to the associated shared memory pool (mandatory)
 * - `map_mode`: the memory mapping mode
 * - `phy_addr`/`virt_addr`: Addresses of the control block in shared memory and current process
 * - `capacity`: Total number of elememts allocated to the queue from the pool
 * - `block_size`: Fixed size of each element's memory block (determines max element count)
 */
typedef struct SharedMemoryPoolQueueConfig_{
    struct SharedMemoryPool *pool;        // Associated shared memory pool
/*
 * < Memory mapping mode (enumerated type). Specifies how the shared
 * memory is mapped into the current process address space (e.g.,
 * direct physical mapping, virtual address translation). Refer to
 * SHARE_MEM_MAP_MODE_* macros for valid values.
 */
    uint16_t                map_mode;
    uint64_t                phy_addr;     // Physical address of control block in shared memory
    uint64_t                virt_addr;    // Virtual address of control block in current process
    size_t                  capacity;     // Total memory size (bytes) allocated to the queue
    size_t                  block_size;   // Size (bytes) of each element's memory block
} SharedMemoryPoolQueueConfig;


/**
 * Macro: Check if map_mode of SharedMemoryPoolQueueConfig is a valid enumerated value
 * Function: Verifies whether the map_mode of the SharedMemoryPoolQueueConfig is correctly assigned to one of the valid values
 * defined in the ShareMemMapMode enumeration, while ensuring the input config pointer is non-null
 * (to avoid null pointer dereference).
 * @param conf Pointer to the SharedMemoryPoolQueueConfig structure whose map_mode member needs to be checked
 * @return Returns 1 (true) if conf is non-null and map_mode is a valid enumerated value; otherwise returns 0 (false)
 */
#define IS_VALID_SHM_CONF_MAP_MODE(conf) \
    ((conf) != NULL && ((conf)->map_mode == SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH \
                    || (conf)->map_mode == SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL))



/**
 * @brief FIFO queue ENQUEUE operation macro (standard C compatible version)
 * 
 * This macro implements the PUSH operation for SharedMemoryPoolQueue. It uses a do...while structure 
 * to ensure syntax compatibility and returns the operation result through the second parameter. 
 * The specific logic is as follows:
 * 1. Increment the header index; if it exceeds max_num_items(capacity), wrap around (circular nature)
 * 2. Check if the operation triggers a queue exception (header equals tail or out of bounds)
 * 3. If abnormal, roll back the header and set an error status; otherwise, set a success status
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure (input)
 * @param result Variable to receive the operation result (output), which can be:
 *               - FRONTEND_PROXY_PROCESS_OK: Operation succeeded
 *               - FRONTEND_PROXY_PROCESS_ERROR: Operation failed (queue is full or abnormal)
 */
#define SHMP_QUEUE_ENQUEUE(queue, result) do { \
    /* Initialize result to success status */ \
    (result) = FRONTEND_PROXY_PROCESS_OK; \
    /* Save original header for rollback in case of exception */ \
    uint16_t original_header = (queue)->header; \
    \
    /* Update header: increment by 1, wrap around if exceeding capacity */ \
    (queue)->header++; \
    if ((queue)->header >= (queue)->capacity) { \
        (queue)->header -= (queue)->capacity; \
    } \
    \
    /* Check for abnormal status: header conflicts with tail or out of bounds */ \
    if ((queue)->header == (queue)->tail || (queue)->header >= (queue)->capacity) { \
        /* Roll back header to pre-operation state */ \
        (queue)->header = original_header; \
        /* Set error result */ \
        (result) = FRONTEND_PROXY_PROCESS_ERROR; \
    } \
} while(0)


/**
 * @brief FIFO-queue DEQUEUE operation macro (standard C compatible version)
 * 
 * This macro implements the DEQUEUE operation for SharedMemoryPoolQueue with priority on empty queue check.
 * It first verifies if the queue is empty before modifying any indices. The logic is:
 * 1. Check if queue is empty (tail equals header) - return error immediately if true
 * 2. Save original tail for rollback in case of subsequent errors
 * 3. Increment the tail index; if exceeding capacity, wrap around (circular nature)
 * 4. Check for out-of-bounds error; rollback and return error if detected
 * 5. Return success status if all operations complete normally
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure (input)
 * @param result Variable to receive the operation result (output), which can be:
 *               - FRONTEND_PROXY_PROCESS_OK: Operation succeeded
 *               - FRONTEND_PROXY_PROCESS_ERROR: Operation failed (queue is empty or abnormal)
 */
#define SHMP_QUEUE_DEQUEUE(queue, result) do { \
    /* First check if queue is empty (tail equals header) */ \
    if ((queue)->tail == (queue)->header) { \
        (result) = FRONTEND_PROXY_PROCESS_ERROR; \
    } else { \
        /* Initialize result to success status */ \
        (result) = FRONTEND_PROXY_PROCESS_OK; \
        /* Save original tail for rollback in case of exception */ \
        uint16_t original_tail = (queue)->tail; \
        \
        /* Update tail: increment by 1 */ \
        (queue)->tail++; \
        \
        /* Handle wrap-around if exceeding capacity */ \
        if ((queue)->tail >= (queue)->capacity) { \
            (queue)->tail -= (queue)->capacity; \
        } \
    } \
} while(0)


/**
 * @brief Macro to get the virtual address of the element pointed by header in the shared memory queue
 * 
 * Calculates the virtual address of the queue element that the header index points to.
 * The address is derived from the queue's base virtual address plus the offset calculated by
 * header index multiplied by block size.
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure
 * @return uint64_t Virtual address of the element at header position
 */
#define SHMP_QUEUE_HEADER_VIRT_ADDR(queue) \
    ((uint64_t)(queue)->virt_addr + (uint64_t)((queue)->header + 1) * (uint64_t)(queue)->block_size)


/**
 * @brief Macro to get the virtual address of the position pointed by tail in the shared memory queue
 * 
 * Calculates the virtual address of the queue position that the tail index points to (next available slot for enqueuing).
 * The address is derived from the queue's base virtual address plus the offset calculated by
 * tail index multiplied by block size.
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure
 * @return uint64_t Virtual address of the position at tail index
 */
#define SHMP_QUEUE_TAIL_VIRT_ADDR(queue) \
    ((uint64_t)(queue)->virt_addr + (uint64_t)((queue)->tail + 1) * (uint64_t)(queue)->block_size)


/**
 * @brief Macro to calculate used memory size using header and tail indices
 * 
 * Computes the total memory occupied by elements in the queue using header and tail indices,
 * without relying on the length field. Follows circular queue logic:
 * - When tail >= header: used elements = tail - header
 * - When tail < header: used elements = (capacity - header) + tail
 * Total used memory is then elements count multiplied by block size.
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure
 * @return size_t Total used memory size in bytes
 */
#define SHMP_QUEUE_USED_MEMORY(queue) \
    ((size_t)( \
        ((queue)->tail >= (queue)->header) ? \
        ((queue)->tail - (queue)->header) : \
        ((queue)->capacity - (queue)->header + (queue)->tail) \
    ) * (size_t)(queue)->block_size)


/**
 * @brief Macro to calculate surplus (available) memory in the shared memory queue
 * 
 * Implements a two-step internal calculation:
 * 1. First compute the number of available slots using header, tail and capacity:
 *    - When tail >= header: surplus slots = capacity - (tail - header)
 *    - When tail < header: surplus slots = header - tail
 * 2. Then multiply available slots by block size to get surplus memory in bytes
 * 
 * @param queue Pointer to the SharedMemoryPoolQueue structure
 * @return size_t Surplus memory size in bytes
 */
#define SHMP_QUEUE_SURPLUS_MEMORY(queue) \
    ((size_t)( \
        /* Step 1: Calculate number of surplus slots */ \
        ((queue)->tail >= (queue)->header) ? \
        ((queue)->capacity - ((queue)->tail - (queue)->header)) : \
        ((queue)->header - (queue)->tail) \
        /* Step 2: Convert slots to memory size */ \
    ) * (size_t)(queue)->block_size)


/**
 * @brief Allocate a memory slot from the queue head
 * @param queue Pointer to the SharedMemoryPoolQueue structure
 * @param addr_ptr Pointer to store the virtual address of allocated slot (output)
 * @return Returns FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 * @note Allocation logic: 
 *       1. Check if queue is full (next header == tail)
 *       2. If not full, store current header's slot address in addr_ptr
 *       3. Update header with wrap-around handling (mod capacity)
 */
#define SHM_POOL_QUEUE_HEAD_ALLOC(queue, addr_ptr) ({ \
    int _status = FRONTEND_PROXY_PROCESS_ERROR; \
    if ((queue != NULL) && (addr_ptr != NULL)) { \
        uint16_t _next_header = (queue->header + 1) % (uint16_t)queue->capacity; \
        if (_next_header != queue->tail) { \
            *addr_ptr = (uintptr_t)(queue->virt_addr1 + (queue->header + 1)* queue->block_size); \
            queue->header = _next_header; \
            _status = FRONTEND_PROXY_PROCESS_OK; \
        } else { \
            *addr_ptr = ERROR_SHARED_MEM_ADDR; \
        } \
    } \
    _status; \
})



/**
 * @brief Looks up the virtual address from table1 or table2 with boundary checks
 * 
 * This macro retrieves the virtual address from either table1 or table2 of the 
 * SharedMemoryPoolQueue structure based on table_id, with validation of input parameters.
 * 
 * @param queue Pointer to SharedMemoryPoolQueue structure
 * @param table_id Table identifier (must be 1 for table1 or 2 for table2)
 * @param idx Index of the entry to look up (must be in range 0 to capacity-1)
 * @param addr_ptr Pointer to store the retrieved virtual address. 
 *                 Set to ERROR_SHARED_MEM_ADDR if parameters are invalid.
 * 
 * @note Performs validation on both table_id and index range. Ensures access
 *       only to valid entries within the bounds defined by queue->capacity.
 */
#define SHM_POOL_QUEUE_LOOKUP_VIRTADDR(queue, table_id, idx, addr_ptr) \
    do { \
        /* Initialize to error value by default */ \
        *(addr_ptr) = ERROR_SHARED_MEM_ADDR; \
        \
        /* Validate table_id is either 1 or 2 */ \
        if (table_id != 1 && table_id != 2) { \
            break; \
        } \
        \
        /* Validate index is within [0, capacity-1] range */ \
        if (idx >= (queue)->capacity || idx < 0) { \
            break; \
        } \
        \
        /* Look up virtual address from the specified valid table */ \
        if (table_id == 1) { \
            *(addr_ptr) = (queue)->table1[idx].virt_addr; \
        } else { /* table_id == 2 */ \
            *(addr_ptr) = (queue)->table2[idx].virt_addr; \
        } \
    } while (0)


/**
 * @def SHM_POOL_QUEUE_ALLOC_FROM_HEADER(queue, addr_ptr)
 * @brief Allocates a memory block from the header of a shared memory pool queue
 * 
 * This macro allocates a memory block from the header position of a shared memory pool queue.
 * It calculates the next header position, checks for available space, and assigns the memory 
 * address based on the queue's memory mapping mode. If allocation fails (e.g., null pointers 
 * or no available space), it sets the address to ERROR_SHARED_MEM_ADDR.
 * 
 * @param[in]  queue     Pointer to the shared memory pool queue structure
 * @param[out] addr_ptr  Pointer to store the allocated memory block's address (as uintptr_t)
 * 
 * @details The allocation process works as follows:
 * 1. Checks if both @p queue and @p addr_ptr are non-null
 * 2. Calculates the next header position using modulo arithmetic with queue capacity
 * 3. Checks if space is available (next header != queue tail)
 * 4. Assigns address based on mapping mode:
 *    - SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH: Directly calculates address from virtual base
 *    - SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL: Uses lookup macro for virtual address
 *    - Other modes: Sets error address
 * 5. Updates queue header to next position on successful allocation
 * 6. Sets @p addr_ptr to ERROR_SHARED_MEM_ADDR on errors (null pointers or no space)
 */
#define SHM_POOL_QUEUE_ALLOC_FROM_HEADER(queue, addr_ptr) \
    do { \
        if ((queue != NULL) && (addr_ptr != NULL)) { \
            uint16_t _next_header = (queue->header + 1) % (uint16_t)queue->capacity; \
            if (_next_header != queue->tail) { \
                if(SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH == queue->map_mode1) {\
                    *addr_ptr = (uintptr_t)(queue->virt_addr1 + (queue->header + 1) * queue->block_size); \
                    queue->header = _next_header; \
                } else if(SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL == queue->map_mode1){\
                    SHM_POOL_QUEUE_LOOKUP_VIRTADDR(queue, 1, queue->header, addr_ptr);\
                    queue->header = _next_header; \
                } else { \
                    *addr_ptr = ERROR_SHARED_MEM_ADDR;\
                } \
        } else { \
            *addr_ptr = ERROR_SHARED_MEM_ADDR; \
        } \
        }\
    }while(0)


/**
 * @brief Roll back the head position of a shared memory pool queue after failed post-allocation processing
 * This macro reverts the head pointer of a shared memory pool queue to its previous position,
 * specifically intended for scenarios where an element was successfully allocated (head pointer incremented),
 * but subsequent processing of the allocated element (e.g., data initialization, validation, or business logic) failed.
 * It restores the queue to a consistent state, preventing invalid allocation markers.
 * @param queue Pointer to the SharedMemoryPoolQueue structure. If NULL, the macro performs no operation.
 * @note Rollback conditions and logic:
 *     No action is taken if the input queue pointer is NULL (safety check)
 *     Rollback is skipped if header == tail (queue is empty), as this would create an invalid underflow state
 *     When rollback is performed:
 *         If header is 0, it wraps around to (capacity - 1) to maintain circular queue semantics
 *         For non-zero header values, it is simply decremented by 1
 * @details Critical usage context:
 * This macro should be invoked only after a successful element allocation (where the head pointer was already advanced)
 * but before completing the element's processing. Common failure scenarios include:
 *     Failed data writing to the allocated element
 *     Validation errors in the data to be stored
 *     Resource shortages during element initialization
 *     Aborted business logic operations after allocation
 * Without this rollback, the queue would retain an advanced head pointer, marking a "used" slot that contains no valid data,
 * leading to lost queue capacity and potential data corruption in subsequent operations.
 * The do-while(0) structure ensures the macro behaves like a single statement, safe for use in if/else blocks without extra braces.
 */
#define SHM_POOL_QUEUE_HEAD_ROLLBACK(queue) do { \
    if (queue != NULL) { \
        if(queue->header == queue->tail) \
            break;\
        queue->header = (queue->header == 0) ? \
            (uint16_t)(queue->capacity - 1) : \
            (queue->header - 1); \
    } \
} while (0)


int init_shared_mem_pool(struct SharedMemoryPool *mem_pool);
void free_shared_mem_pool(struct SharedMemoryPool *mem_pool);
uint64_t alloc_shared_mem(struct SharedMemoryPool *mem_pool);
void free_shared_mem(struct SharedMemoryPool *mem_pool, uint64_t addr);

int init_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool);
void free_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool);
int fetch_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool);
int release_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool);



struct SharedMemoryPoolQueue *shared_mem_pool_queue_create_frontend(const SharedMemoryPoolQueueConfig *config);

int shared_mem_pool_queue_frontend_setup_pages(struct SharedMemoryPoolQueue *queue, uint32_t page_num, uint64_t page_phy[]);

int shared_mem_pool_queue_initialize(struct SharedMemoryPoolQueue *queue, const SharedMemoryPoolQueueConfig *config);

int shared_mem_pool_queue_destroy(struct SharedMemoryPoolQueue* queue);



int shared_mem_pool_queue_send(struct SharedMemoryPoolQueue *queue, 
                               const void *data, 
                               size_t data_size);

/**
 * @brief Sends a variable-size block (up to block_size) to the SharedMemoryPoolQueue: One Copy
 * 
 * Stores a variable-length data block (with size ≤ block_size) into the queue using one-copy semantics.
 * The data is transferred to a shared memory slot through a single copy operation. Regardless of the 
 * actual data size, the entire block_size space in shared memory is occupied to maintain fixed-size 
 * slot management. Explicitly handles full queue cases.
 * 
 * @param queue        Pointer to the SharedMemoryPoolQueue instance
 * @param data         Input pointer to the variable-length data block to be sent. Must point to valid memory 
 *                     containing the data (not a pointer to a pointer).
 * @param data_size    Size of the input data block (must be > 0 and ≤ queue->block_size for valid operation)
 * 
 * @return int Returns:
 *             - FRONTEND_PROXY_PROCESS_OK: Success, data was copied to shared memory and queue state updated
 *             - FRONTEND_PROXY_PROCESS_AGAIN: Queue is full (next tail position equals header)
 *             - FRONTEND_PROXY_PROCESS_ERROR: Invalid input parameters (NULL pointers for queue or data), 
 *                                           invalid block_size (0 bytes), data_size = 0, or data_size > queue->block_size
 * 
 * @note The input data block (pointed to by data) must remain valid until the copy operation completes. 
 *       One-copy semantics involve a single data transfer (e.g., via memcpy) from the input buffer to the 
 *       target shared memory slot. 
 *       Critical behavior: Even if data_size < block_size, the entire block_size space in shared memory is 
 *       reserved and counted as occupied (surplus decreases by block_size, not data_size).
 *       Queue state (tail, length, surplus) is updated via the SHMP_QUEUE_ENQUEUE macro after the copy completes.
 *       Address calculation: Target slot address is derived from queue->virt_addr + (tail * block_size).
 */
int shared_mem_pool_queue_send_oc(struct SharedMemoryPoolQueue *queue, 
                                  const void *data, 
                                  size_t data_size);


int shared_mem_pool_queue_recv_zc(struct SharedMemoryPoolQueue *queue,
                              void **buffer,
                              size_t *out_data_size);


/**
 * @brief Acquire access right to the shared memory queue by obtaining the shared memory pool lock
 * @param queue Pointer to the SharedMemoryPoolQueue instance to be accessed
 * @return FRONTEND_PROXY_PROCESS_OK if lock is acquired successfully;
 *         FRONTEND_PROXY_PROCESS_ERROR if a system-level error occurs (e.g., invalid pool handle);
 *         FRONTEND_PROXY_PROCESS_AGAIN if lock acquisition times out (retry may succeed)
 * @note Retrieves the lock associated with the shared memory pool (queue->pool) to control queue access;
 *       Must be paired with SHARED_MEM_QUEUE_UNLOCK using the same queue to prevent deadlocks;
 *       FRONTEND_PROXY_PROCESS_AGAIN indicates temporary unavailability - callers should retry later
 */
//#define SHARED_MEM_QUEUE_LOCK(queue)  fetch_shared_mem_pool_lock((queue)->pool)
#define SHARED_MEM_QUEUE_LOCK(queue)  fetch_shared_mem_pool_lock(&((queue)->pool->lock))


/**
 * @brief Release access right to the shared memory queue by releasing the shared memory pool lock
 * @param queue Pointer to the SharedMemoryPoolQueue instance that was accessed
 * @return FRONTEND_PROXY_PROCESS_OK if lock is released successfully;
 *         FRONTEND_PROXY_PROCESS_ERROR if a system-level error occurs (e.g., releasing an unheld lock)
 * @note Releases the lock associated with the shared memory pool (queue->pool) to end controlled access;
 *       Must be paired with SHARED_MEM_QUEUE_LOCK using the same queue to prevent deadlocks;
 *       Does not return FRONTEND_PROXY_PROCESS_AGAIN - release operation either succeeds or fails
 */
//#define SHARED_MEM_QUEUE_UNLOCK(queue)  (release_shared_mem_pool_lock((queue)->pool))
#define SHARED_MEM_QUEUE_UNLOCK(queue)  (release_shared_mem_pool_lock(&(queue)->pool->lock))

struct SharedMemoryPoolQueue *shared_mem_pool_queue_create_frontendend(const SharedMemoryPoolQueueConfig *config);

#endif