#include "shared_mem_io.h"
#include "frontend_proto.h"
// #include "hyperamp_shm_queue.h"


int init_shared_mem_pool(struct SharedMemoryPool *mem_pool){
    return FRONTEND_PROXY_PROCESS_OK;
}

void free_shared_mem_pool(struct SharedMemoryPool *mem_pool){

}


uint64_t alloc_shared_mem(struct SharedMemoryPool *mem_pool){
    if(NULL == mem_pool)
        return ERROR_SHARED_MEM_ADDR;

    return 0;
}


void free_shared_mem(struct SharedMemoryPool *mem_pool, uint64_t addr){
    
}



int init_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool){
    return FRONTEND_PROXY_PROCESS_OK;
}



void free_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool){

}



int fetch_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool){
    return FRONTEND_PROXY_PROCESS_OK;
}


int release_shared_mem_pool_lock(struct SharedMemoryPoolLock *mem_pool){
    return FRONTEND_PROXY_PROCESS_OK;
}




/**
 * Create and initialize a shared memory pool queue in frontend side.
 * 
 * @param config Pointer to the SharedMemoryPoolQueueConfig structure containing configuration
 * parameters (e.g., underlying shared memory pool, lock settings) for queue creation.
 * @return Pointer to the newly created SharedMemoryPoolQueue on success; NULL on failure
 * (e.g., invalid config, insufficient memory for queue instance)
 */
struct SharedMemoryPoolQueue *shared_mem_pool_queue_create_frontend(const SharedMemoryPoolQueueConfig *config){
    struct SharedMemoryPoolQueue *queue;
    int fd;
    void *virt_addr;

    if(!IS_VALID_SHM_CONF_MAP_MODE(config)){
        error_print("shared_mem_pool_queue_create_frontend failed: invalid config (NULL pointer), or invalid map mode\n");
        return NULL;
    }

    queue = malloc(sizeof(struct SharedMemoryPoolQueue));

    if(NULL == queue){
        error_print("shared_mem_pool_queue_create_frontend failed: failed to allocate memory for SharedMemoryPoolDeque instance\n");
        return NULL;
    }

//    queue->pool         = pool;
//    queue->length       = 0;

    queue->map_mode1    = config->map_mode;
    queue->pool         = config->pool;
    queue->block_size   = config->block_size;
    queue->capacity     = config->capacity;
    queue->virt_addr1   = config->virt_addr;
    queue->phy_addr     = config->phy_addr;
    queue->header       = 0;
    queue->tail         = 0;

#if 0
    fd = open("/dev/mem", O_RDWR | O_SYNC);

    if(fd < 0){
        error_print("shared_mem_pool_queue_create_frontend failed: failed to open /dev/mem");
        free(queue);
        return NULL;
    }
#endif
    // Calculate page-aligned physical address (mmap requires the offset to be a multiple of the page size)
    off_t phys_page_off = queue->phy_addr & ~(sysconf(_SC_PAGESIZE) - 1);
    size_t page_offset  = queue->phy_addr - phys_page_off;

    utils_print("phys_page_off = %d, page_offset = %d\n", phys_page_off, page_offset);

    // Map physical memory to user space
#if 0
    virt_addr = mmap(
        NULL,               // Let the kernel automatically allocate virtual address
        queue->capacity * queue->block_size + page_offset,  // Mapping size (including intra-page offset)
//        queue->block_size + page_offset,  // Mapping size (including intra-page offset)
//        PROT_READ | PROT_WRITE,  // Read and write permissions
        PROT_READ,  // Read and write permissions
//        MAP_SHARED,         // Shared mapping
        MAP_SHARED  ,         // Shared mapping
        fd,                 // File descriptor for /dev/mem
        phys_page_off       // Page-aligned physical address
    );

    utils_print("errno = %d, reasion is %s\n", errno, strerror(errno));

    if (virt_addr == MAP_FAILED) {
        error_print("shared_mem_pool_queue_create_frontend failed: mmap failed");
        close(fd);
        free(queue);
        return NULL;
    }
#endif

    virt_addr = malloc(config->block_size * (config->capacity + 1));

    if(NULL == virt_addr){
        error_print("shared_mem_pool_queue_create_frontend failed: failed to allocate memory for the queue!");
        close(fd);
        free(queue);
        return NULL;
    }

    queue->virt_addr1 = virt_addr;

    utils_print("queue->virt_addr1 = %lld\n", queue->virt_addr1);

    return queue;
}




/**
 * @brief Initializes a SharedMemoryPoolQueue instance with specified configuration
 * 
 * This function initializes all members of a SharedMemoryPoolQueue structure using parameters
 * provided in the configuration. It validates input, copies base parameters, calculates derived
 * properties (like max element count), and sets initial queue state (empty state with zero length).
 * 
 * @param queue  Pointer to the SharedMemoryPoolQueue instance to be initialized
 * @param config Pointer to a SharedMemoryPoolQueueConfig structure containing initialization parameters
 * 
 * @return int Returns FRONTEND_PROXY_PROCESS_OK if initialization succeeds; 
 *             Returns FRONTEND_PROXY_PROCESS_ERROR if input parameters are invalid (NULL pointers)
 * 
 * @note Derived parameters (capacity, surplus) are calculated based on 
 *       capacity and block_size to ensure consistent queue state. Invalid inputs 
 *       prevent any modification to the queue instance.
 */
int shared_mem_pool_queue_initialize(struct SharedMemoryPoolQueue *queue, const SharedMemoryPoolQueueConfig *config){
    // Validate input parameters to prevent null pointer dereference
    if (NULL == queue || NULL == config || NULL == config->pool) {
        error_print("shared_mem_pool_queue_initialize failed: input parameter(s) is/are NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Validate block size is positive to avoid division by zero in capacity calculation
    if(0 == config->block_size){
            error_print("shared_mem_pool_queue_initialize failed: block size should be a positive number!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Initialize base members from configuration parameters
    queue->pool = config->pool;
    queue->phy_addr = config->phy_addr;
    queue->virt_addr1 = config->virt_addr;
    queue->capacity = config->capacity;
    queue->block_size = config->block_size;
    
    // Calculate maximum number of items (avoid division by zero)
    queue->capacity = config->block_size;
    
    // Initialize queue to empty state
    queue->header = 0;                  // Start with head at initial position
    queue->tail = 0;                    // Start with tail at initial position
//    queue->length = 0;                  // No elements in empty queue
//    queue->surplus = config->capacity;  // All slots available initially

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Perform address mapping for physical memory pages allocated to the shared memory pool queue
 *
 * This function establishes address mapping relationships for the physical memory pages allocated to the shared memory pool queue,
 * adapting to the address space differences between the frontend (microkernel-OS) and backend (Linux):
 * - Frontend (microkernel-OS): Updates the corresponding address mapping table (table2) or virtual address (virt_addr2)
 *   based on the queue's map_mode2 configuration.
 * - Backend (Linux): Updates the corresponding address mapping table (table1) or virtual address (virt_addr1)
 *   based on the queue's map_mode1 configuration.
 * After successful mapping, both the frontend and backend can access the queue's shared physical memory through their respective
 * virtual addresses or mapping tables, ensuring memory accessibility for FIFO data transmission.
 *
 * @param[in,out] queue Pointer to the shared memory pool queue instance for which physical pages need to be mapped.
 *                      Must contain valid configuration information such as mapping modes (map_mode1/map_mode2) and
 *                      address mapping tables (table1/table2). The mapping results will be updated to the virtual address
 *                      fields (virt_addr1/virt_addr2) or address mapping tables of this structure.
 * @param[in]     page_num Total number of physical pages to be mapped (unsigned 32-bit integer). Must be greater than 0 and
 *                      consistent with the length of the page_phy array, serving as the count basis for the mapping operation.
 * @param[in]     page_phy Array storing the physical addresses of the pages to be mapped. Each element corresponds to the
 *                      starting physical address of a physical page. The length of the array must be equal to page_num,
 *                      and the addresses must be valid physical addresses allocated from the shared memory pool.
 *
 * @return FRONTEND_PROXY_PROCESS_OK Mapping succeeded. All specified physical pages have completed front-end and back-end
 *                                  address association, and the relevant address fields of the queue have been updated.
 * @return FRONTEND_PROXY_PROCESS_ERROR Mapping failed. Possible reasons include:
 *                                      1. queue is a null pointer or page_num == 0;
 *                                      2. page_phy is a null pointer or contains invalid physical addresses;
 *                                      3. Invalid configuration of mapping modes (map_mode1/map_mode2);
 *                                      4. Failure to execute address space mapping operations on the front-end or back-end;
 *                                      5. Insufficient storage space in the address mapping tables (table1/table2).
 *
 * @note 1. The frontend runs on microkernel-OS and the backend runs on Linux; the mapping operation must adapt to the memory
 *          management mechanisms of both sides respectively.
 *       2. The mapping logic depends on the queue's map_mode1 (Linux side) and map_mode2 (microkernel side) configurations,
 *          which must be initialized in advance.
 *       3. Physical page addresses must be allocated from the shared memory pool associated with the queue (pool field) to
 *          ensure address validity and shared accessibility.
 *       4. If the mapping mode is SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH, virt_addr1/virt_addr2 will be updated;
 *          if it is SHARE_MEM_MAP_MODE_CONTIGUOUS_PHYS_DISCRETE_LOGICAL, the table1/table2 mapping tables will be populated.
 */
int shared_mem_pool_queue_frontend_setup_pages(struct SharedMemoryPoolQueue *queue, uint32_t page_num, uint64_t page_phy[]){
    int page_cnt = 0;
/*
 * Validate core parameter validity: queue/physical address array is non-null, page count is in valid range (1~MAX_MAP_TABLE_ENTRY_COUNT)
 */
    if(NULL == queue || NULL == page_phy || 0 == page_num || page_num > MAX_MAP_TABLE_ENTRY_COUNT){
        error_print("shared_mem_pool_queue_frontend_setup_pages failed: invalid input parameters - queue/page_phy NULL or invalid page_num!\n");
    }

    

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * Destroy a shared memory pool queue and release associated resources.
 * 
 * @param queue Pointer to the SharedMemoryPoolDeque to destroy. Passing NULL is safe (no operation).
 * @return FRONTEND_PROXY_PROCESS_OK on success;  
 *         FRONTEND_PROXY_PROCESS_ERROR on failure.
 */
int shared_mem_pool_queue_destroy(struct SharedMemoryPoolQueue* queue){
    return FRONTEND_PROXY_PROCESS_OK;
}




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
 *             - FRONTEND_PROXY_PROCESS_ERROR: Success, data was copied to shared memory and queue state updated
 *             - FRONTEND_PROXY_PROCESS_AGAIN: Queue is full (next tail position equals header)
 *             - FRONTEND_PROXY_PROCESS_OK: Invalid input parameters (NULL pointers for queue or data), 
 *                                            invalid block_size (0 bytes), data_size = 0, or data_size > queue->block_size
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
                                  size_t data_size)
{
    uint8_t     *base_addr;
    int         ret;

    // Validate input parameters
    if (NULL == queue || NULL == data) {
        error_print("shared_mem_pool_queue_send_oc failed: invalid input parameters (NULL pointers)!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Validate block size configuration
    if (queue->block_size == 0) {
        error_print("shared_mem_pool_queue_send_oc failed: invalid block size (0 bytes)!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Validate input data size range
    if (data_size == 0 || data_size > queue->block_size) {
        error_print("shared_mem_pool_queue_send_oc failed: invalid data size!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    /*
     * Check if queue is full (next header position equals tail)
     * Queue uses header for writing and tail for reading: (header+1) % max == tail means full
     */
    if ((queue->header + 1) % queue->capacity == queue->tail) {
        error_print("shared_mem_pool_queue_send_oc: queue is full!");
        return FRONTEND_PROXY_PROCESS_AGAIN;
    }

    // Calculate target address in shared memory (write to header position)
    base_addr = (uint8_t *)queue->virt_addr1;
    
    // Perform one-copy operation to shared memory slot
    memcpy(base_addr + queue->header * queue->block_size, data, data_size);

    // Update queue state via macro (moves header forward and adjusts length/surplus)
    SHMP_QUEUE_ENQUEUE(queue, ret);

    return ret;
}



/**
 * @brief Receives a fixed-size block from the SharedMemoryPoolQueue: Zero Copy
 * 
 * Retrieves one fixed-size memory block (of size block_size) from the front of the queue using zero-copy semantics.
 * The data is directly accessed from the shared memory slot without copying by returning a pointer to the memory 
 * location in the shared pool. Explicitly handles empty queue cases.
 * 
 * @param queue        Pointer to the SharedMemoryPoolQueue instance
 * @param buffer       Output parameter to store the pointer to the received block. Points directly to shared memory.
 * @param out_data_size Output parameter to store the actual size of the received block (always equal to queue->block_size)
 * 
 * @return int Returns:
 *             - FRONTEND_PROXY_PROCESS_OK: Success, a block was retrieved and queue state updated
 *             - FRONTEND_PROXY_PROCESS_AGAIN: Queue is empty (header == tail)
 *             - FRONTEND_PROXY_PROCESS_ERROR: Invalid input parameters (NULL pointers) or invalid block size (0 bytes)
 * 
 * @note The returned buffer MUST NOT be freed, as it points to shared memory. The buffer's validity is managed by 
 *       queue operations. out_data_size is always set to queue->block_size for successful operations.
 *       Queue state (header, length, surplus) is updated via the SHMP_QUEUE_DEQUEUE macro after retrieving the block.
 *       Address calculation: Block address is derived from queue->virt_addr + (tail * block_size).
 */
int shared_mem_pool_queue_recv_zc(struct SharedMemoryPoolQueue *queue,
                              void **buffer,
                              size_t *out_data_size) {
    uint8_t     *base_addr;
    int         ret;
    // Validate input parameters
    if (NULL == queue || NULL == buffer || NULL == out_data_size) {
        error_print("shared_mem_pool_queue_recv_zc failed: invalid input parameters (NULL pointers)");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Validate block size (should never happen if init was successful)
    if (queue->block_size == 0) {
        error_print("shared_mem_pool_queue_recv_zc failed: invalid block size (0 bytes)");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

        // Check if queue is empty (header == tail and length == 0)
    if (queue->header == queue->tail) {
        error_print("shared_mem_pool_queue_recv_zc: queue is empty");
        return FRONTEND_PROXY_PROCESS_AGAIN;
    }

    base_addr       = (uint8_t *)queue->virt_addr1;
    *buffer         = base_addr + (queue->tail + 1) * queue->block_size;
    *out_data_size  = queue->block_size;

    utils_print("In %s, message address is %p\n", __func__, *buffer);

    SHMP_QUEUE_DEQUEUE(queue, ret);

    return ret;
}
