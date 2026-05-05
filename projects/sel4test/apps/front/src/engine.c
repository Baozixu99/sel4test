#include "engine.h"
#include <platsupport/delay.h>
#include <sel4/sel4.h>
extern FrontendEngine *p_g_fr_eng;
extern FrontendEngine g_fr_eng;

volatile HyperampShmQueue *g_hyper_tx_queue = NULL;  // seL4 → Linux (seL4 writes requests, Linux reads)
volatile HyperampShmQueue *g_hyper_rx_queue = NULL;  // Linux → seL4 (seL4 reads responses, Linux writes)
volatile void *g_hyper_data_region          = NULL;  // Shared data buffer referenced by entries in TX/RX queues

/* Multi-channel layout: CH0 base + 0x200000 = CH1 base, +0x300000 = CH2 base */
#define HYPERAMP_CH1_OFFSET_PADDR   0x200000UL
#define HYPERAMP_CH0_QUEUE_CAP      256
/* CH1 total is 1MB; data_region = 1MB - 8KB = 254 blocks; enqueue uses (idx+1)*block_size => cap <= 253 */
#define HYPERAMP_CH1_QUEUE_CAP      253

#if 0
HyperampQueueConfig hyper_tx_config = {
        .map_mode = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
        .capacity = 256,
        .block_size = 4096,
        .phy_addr = SHM_TX_QUEUE_PADDR,
        .virt_addr = (uint64_t)g_hyper_tx_queue,
};
#endif

/**
 * Configuration structure for the high-speed network receive queue.
 * 
 * This global instance initializes parameters for managing the receive queue's shared memory,
 * including memory mapping mode, addresses, capacity, and block size.
 */
SharedMemoryPoolQueueConfig high_speed_net_rx_queue_config =     {
    .pool           = NULL,
    .map_mode       = SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH,  // Assumes virtual address mapping mode
    .phy_addr       = HSNET_RX_PHY_ADDR_BASE,              // 64-bit unsigned integer zero value
    .virt_addr      = 0ULL,                                // 64-bit unsigned integer zero value
    .capacity       = MAX_MAP_TABLE_ENTRY_COUNT,           // Total number of element 
    .block_size     = 4096                                 // 4096 bytes per element block
};


/**
 * Configuration structure for the high-speed network transmit queue.
 * 
 * This global instance initializes parameters for managing the transmit queue's shared memory,
 * including memory mapping mode, addresses, capacity, and block size.
 */
SharedMemoryPoolQueueConfig high_speed_net_tx_queue_config =    {
    .pool           = NULL,
    .map_mode       = SHARE_MEM_MAP_MODE_CONTIGUOUS_BOTH,  // Assumes virtual address mapping mode
    .phy_addr       = HSNET_TX_PHY_ADDR_BASE,              // 64-bit unsigned integer zero value
    .virt_addr      = 0ULL,                                // 64-bit unsigned integer zero value
    .capacity       = MAX_MAP_TABLE_ENTRY_COUNT,           // Total number of element 
    .block_size     = 4096                                 // 4096 bytes per element block
};


/**
 * Configuration structure for the Hyperamp shared transmit queue (seL4 → Linux).
 *
 * This global instance defines the parameters required to set up the transmit queue's
 * shared memory region, including the mapping mode, physical and virtual addresses,
 * queue capacity, and block size.
 */
HyperampQueueConfig hyperamp_tx_config = {
    .map_mode = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
    .capacity = 256,
    .block_size = 4096,
    .phy_addr = SHM_TX_QUEUE_PADDR,
    .virt_addr = (uint64_t)SHM_TX_QUEUE_VADDR,
};


/**
 * Configuration structure for the Hyperamp shared receive queue (Linux → seL4).
 *
 * This global instance defines the parameters required to set up the receive queue's
 * shared memory region, including the mapping mode, physical and virtual addresses,
 * queue capacity, and block size.
 */
HyperampQueueConfig hyperamp_rx_config = {
    .map_mode = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH,
    .capacity = 256,
    .block_size = 4096,
    .phy_addr = SHM_RX_QUEUE_PADDR,
    .virt_addr = (uint64_t)SHM_RX_QUEUE_VADDR,
};

/**
 * @brief Get data from the specified RX queue (residing in shared memory)
 * @param queue Pointer to the SharedMemoryPoolQueue (RX queue) to operate on
 * @param[out] buf_ptr Double pointer to store the address of data in shared memory
 *                     (points to actual data location in shared memory on success)
 * @param buf_max_len Maximum allowed length of data that can be retrieved (in bytes)
 * @param[out] out_len Pointer to store the actual length of obtained data (in bytes)
 * @return FRONTEND_PROXY_PROCESS_OK if data is retrieved successfully;
 *         FRONTEND_PROXY_PROCESS_ERROR if a system-level error occurs (e.g., invalid queue handle);
 *         FRONTEND_PROXY_PROCESS_AGAIN if data is temporarily unavailable (e.g., queue is empty)
 * @note The caller is responsible for managing the lock of the shared memory pool 
 *       (lock once before multiple calls to reduce overhead)
 */
int frontend_engine_rx_queue_get(struct SharedMemoryPoolQueue *queue, void **buf_ptr, 
                               size_t buf_max_len, size_t *out_len){
    int             ret;
    size_t          msg_size, buf_size;
    ProxyMsgHeader  *msg_hdr;

    if(NULL == queue || NULL == buf_ptr || NULL == out_len){
        error_print("frontend_engine_rx_queue_get failed: invalid input parameters (NULL pointers)");
        return FRONTEND_PROXY_PROCESS_ERROR;   
    }

    utils_print("In %s, before call shared_mem_pool_queue_recv_zc, buf_ptr = %p, *buf_ptr = %p\n", __func__, buf_ptr, *buf_ptr);
    ret = shared_mem_pool_queue_recv_zc(queue, buf_ptr, &buf_size);
    utils_print("In %s, after call shared_mem_pool_queue_recv_zc, buf_ptr = %p, *buf_ptr = %p\n", __func__, buf_ptr, *buf_ptr);

    if(FRONTEND_PROXY_PROCESS_ERROR == ret){
        error_print("frontend_engine_rx_queue_get failed: failed to retrieve data from RX queue");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
        error_print("frontend_engine_rx_queue_get returns: the RX queue is empty");
        return FRONTEND_PROXY_PROCESS_AGAIN;
    }

    msg_hdr     = (ProxyMsgHeader *)(*buf_ptr);
    msg_size    = msg_hdr->payload_len;

    if(msg_size + sizeof(ProxyMsgHeader) > buf_max_len){
        error_print("frontend_engine_rx_queue_get failed: message total size (header + payload) exceeds buffer maximum length");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    *out_len = msg_size;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Retrieves a message from the HyperAMP receive queue managed by the FrontendEngine.
 *
 * @details This function fetches message data from the HyperAMP RX queue associated with the given
 *          FrontendEngine instance, and copies the message data to the provided buffer pointed to by @p data.
 *          It returns an integer status code to indicate the operation result, and outputs the actual 
 *          length of the retrieved message through the @p out_len parameter.
 *          The caller must check the returned status code before using the data in @p data and the value in @p out_len.
 *
 * @param[in]  eng         Pointer to the FrontendEngine instance, cannot be NULL
 * @param[in]  max_msg_len Maximum allowed length of the message to read (i.e., the size of the @p data buffer), 
 *                         used to prevent buffer overflow
 * @param[out] data        Pointer to a uint8_t buffer that stores the retrieved message data; 
 *                         valid only when the return value is FRONTEND_PROXY_PROCESS_OK
 * @param[out] out_len     Pointer to a size_t variable that stores the actual length of the retrieved message;
 *                         valid only when the return value is FRONTEND_PROXY_PROCESS_OK
 *
 * @return Integer status code indicating the result of the operation:
 *         - FRONTEND_PROXY_PROCESS_OK: Operation succeeded
 *         - FRONTEND_PROXY_PROCESS_ERROR: System-level error occurred
 *         - FRONTEND_PROXY_PROCESS_AGAIN: Message temporarily unavailable
 *
 * @retval FRONTEND_PROXY_PROCESS_OK
 *         Message data is retrieved successfully, the @p data buffer contains valid message content
 *         and @p out_len holds the actual message length
 * @retval FRONTEND_PROXY_PROCESS_ERROR
 *         A system-level error occurred (e.g., engine is NULL, internal queue not initialized, @p data/@p out_len is NULL),
 *         the @p data buffer and @p out_len are undefined
 * @retval FRONTEND_PROXY_PROCESS_AGAIN
 *         Message data is temporarily unavailable (e.g., HyperAMP RX queue is empty),
 *         the @p data buffer and @p out_len are undefined
 *
 * @note The message data copied to @p data is sourced from shared memory (e.g., @c g_hyper_data_region).
 *       The ownership of the @p data buffer is held by the caller (who is responsible for allocating/freeing it),
 *       while the underlying shared memory lifecycle follows the HyperAMP queue protocol.
 *       Refer to HyperAMP documentation for detailed memory and lifecycle management rules.
 */
int frontend_engine_hyperamp_rx_queue_get(FrontendEngine *eng, size_t max_msg_len, 
                                          uint8_t *data, size_t *out_len){
    int ret;
    if(NULL == eng || NULL == eng->hyper_rx_queue || NULL == data || NULL == out_len){
        error_print("frontend_engine_hyperamp_rx_queue_get failed: invalid input parameters (NULL pointers)\n");
        return FRONTEND_PROXY_PROCESS_ERROR;   
    }

    if(NULL == g_hyper_data_region){
        error_print("frontend_engine_hyperamp_rx_queue_get failed: The HyperAMP shared memory region is not initialized!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;  
    }

    /* 关键：在读取队列状态前失效缓存，确保读取到 Linux 写入的最新数据 */
    hyperamp_cache_invalidate(eng->hyper_rx_queue, 64);
        
    // 检查 RX Queue 是否有来自 Linux 后端的响应
    uint16_t rx_header = hyperamp_safe_read_u16(eng->hyper_rx_queue,
                                                     offsetof(HyperampShmQueue, header));
    uint16_t rx_tail = hyperamp_safe_read_u16(eng->hyper_rx_queue,
                                                   offsetof(HyperampShmQueue, tail));
    
    volatile void *rx_data_base = g_hyper_data_region;  // 共享数据区
    printf("debug: rx_header=%u, rx_tail=%u, rx_data_base=%p, msg_buf=%p\n", rx_header, rx_tail, rx_data_base, data);
    ret = hyperamp_queue_dequeue(eng->hyper_rx_queue, HYPERAMP_ZONE_ID_SEL4, data, max_msg_len, out_len, rx_data_base);
    
    if(HYPERAMP_ERROR == ret){
        error_print("frontend_engine_hyperamp_rx_queue_get failed: hyperamp_queue_dequeue execution failed!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;  
    }else if(HYPERAMP_AGAIN == ret){
        return FRONTEND_PROXY_PROCESS_AGAIN;
    }else{
/* 
 * hyperamp_queue_dequeue execution succeeded, no action required. 
 */
    }

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Sends a message to the HyperAMP transmit queue managed by the FrontendEngine.
 *
 * @details This function writes message data from the provided buffer (pointed to by @p data) into the 
 *          HyperAMP TX queue associated with the given FrontendEngine instance. It validates the input 
 *          parameters and message length to ensure compliance with HyperAMP queue constraints, then 
 *          copies the message data to the shared memory region of the TX queue.
 *          It returns an integer status code to indicate the operation result, and the caller must check 
 *          this code to confirm if the message was successfully enqueued.
 *
 * @param[in]  eng         Pointer to the FrontendEngine instance, cannot be NULL
 * @param[in]  msg_len     Actual length of the message to send (i.e., the size of valid data in the @p data buffer), 
 *                         must be greater than 0 and not exceed the maximum capacity of the HyperAMP TX queue
 * @param[in]  data        Pointer to a uint8_t buffer containing the message data to send; 
 *                         cannot be NULL and must hold valid data of length @p msg_len
 *
 * @return Integer status code indicating the result of the operation:
 *         - FRONTEND_PROXY_PROCESS_OK: Operation succeeded
 *         - FRONTEND_PROXY_PROCESS_ERROR: System-level error occurred
 *         - FRONTEND_PROXY_PROCESS_AGAIN: Message temporarily cannot be sent
 *
 * @retval FRONTEND_PROXY_PROCESS_OK
 *         Message data is written to the HyperAMP TX queue successfully, and the backend can retrieve it
 *         via its HyperAMP RX queue
 * @retval FRONTEND_PROXY_PROCESS_ERROR
 *         A system-level error occurred (e.g., engine is NULL, internal queue not initialized, @p data is NULL,
 *         @p msg_len is 0 or exceeds queue capacity), the message was not enqueued
 * @retval FRONTEND_PROXY_PROCESS_AGAIN
 *         Message cannot be sent temporarily (e.g., HyperAMP TX queue is full), the caller may retry later,
 *         no data was written to the queue
 *
 * @note The message data copied from @p data is stored in shared memory (e.g., @c g_hyper_data_region).
 *       The ownership of the @p data buffer is held by the caller (who is responsible for allocating/freeing it),
 *       while the underlying shared memory lifecycle follows the HyperAMP queue protocol.
 *       Refer to HyperAMP documentation for detailed memory and lifecycle management rules.
 *       This function is the reverse operation of frontend_engine_hyperamp_rx_queue_get: the former reads from
 *       the RX queue, while this function writes to the TX queue.
 */
int frontend_engine_hyperamp_tx_queue_put(FrontendEngine *eng, size_t msg_len, 
                                          const uint8_t *data){
        int ret;

    if(NULL == eng){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: eng is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(NULL == eng->hyper_tx_queue){
        error_print("frontend_engine_hyperamp_tx_queue_put failed:  eng->hyper_tx_queue is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(msg_len > eng->hyper_tx_queue->block_size){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: message length exceeds queue block size limit!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == eng->hyper_amp_data_region){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: eng->hyper_amp_data_region is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == data){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: data is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    

    utils_print("In %s, the address of hyper_tx_queue is %p\n", __func__, eng->hyper_tx_queue);

    ret = hyperamp_queue_enqueue(eng->hyper_tx_queue, HYPERAMP_ZONE_ID_SEL4, data, msg_len, eng->hyper_amp_data_region);

    if(HYPERAMP_ERROR == ret){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: hyperamp_queue_enqueue execution failed!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;  
    }else if(HYPERAMP_AGAIN == ret){
        error_print("frontend_engine_hyperamp_tx_queue_put failed: queue is empty!\n");
        return FRONTEND_PROXY_PROCESS_AGAIN;
    }else{
/* 
 * hyperamp_queue_enqueue execution succeeded, no action required. 
 */
    }


    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Retrieves a message from the shared memory pool queue.
 *
 * @details This function fetches message data from the specified SharedMemoryPoolQueue instance.
 *          It returns a pointer to the message buffer if data is available, and outputs the
 *          operation status and actual message length through output parameters.
 *          The caller should check the status code stored in @p ret before using the returned buffer
 *          and message length.
 *
 * @param[in]  queue       Pointer to the SharedMemoryPoolQueue instance, cannot be NULL
 * @param[in]  max_msg_len Maximum allowed length of the message to read, used to prevent buffer overflow
 * @param[out] ret         Pointer to an integer variable that stores the operation status code
 * @param[out] out_len     Pointer to a size_t variable that stores the actual length of the retrieved message
 *
 * @return Pointer to the uint8_t message data buffer on successful data retrieval;
 *         NULL if the operation fails, the queue is empty, or a system error occurs
 *
 * @retval FRONTEND_PROXY_PROCESS_OK
 *         Message data is retrieved successfully, the returned pointer and @p out_len are valid
 * @retval FRONTEND_PROXY_PROCESS_ERROR
 *         A system-level error occurred (e.g., invalid queue handle, NULL input pointers),
 *         the returned pointer is NULL and @p out_len is undefined
 * @retval FRONTEND_PROXY_PROCESS_AGAIN
 *         Message data is temporarily unavailable (e.g., queue is empty),
 *         the returned pointer is NULL and @p out_len is undefined
 *
 * @note The ownership and release mechanism of the returned data buffer depend on the implementation
 *       of the SharedMemoryPoolQueue module; refer to the module's documentation for memory management rules.
 */
uint8_t *frontend_engine_rx_queue_get_msg(struct SharedMemoryPoolQueue *queue, size_t max_msg_len, 
                                          int *ret, size_t *out_len){
    size_t          msg_size, buf_size;
    ProxyMsgHeader  *msg_hdr;
    uint8_t         *buff_addr;
    int             ret_val;

    if(NULL == queue || NULL == ret || NULL == out_len){
        error_print("frontend_engine_rx_queue_get_msg failed: invalid input parameters (NULL pointers)\n");
        *ret        = FRONTEND_PROXY_PROCESS_ERROR;
        *out_len    = 0;
        return NULL;   
    }

    utils_print("In %s, before call shared_mem_pool_queue_recv_zc, buf_ptr = %p\n", __func__, buff_addr);
    ret_val = shared_mem_pool_queue_recv_zc(queue, &buff_addr, &buf_size);
    utils_print("In %s, after call shared_mem_pool_queue_recv_zc, buf_ptr = %p\n", __func__, buff_addr);

    if(FRONTEND_PROXY_PROCESS_ERROR == ret_val){
        error_print("frontend_engine_rx_queue_get_msg failed: failed to retrieve data from RX queue\n");
        *ret = FRONTEND_PROXY_PROCESS_ERROR;
        *out_len = 0;
        return NULL;
    }

    if(FRONTEND_PROXY_PROCESS_AGAIN == ret_val){
        error_print("frontend_engine_rx_queue_get_msg failed: the RX queue is empty\n");
        *ret        = FRONTEND_PROXY_PROCESS_AGAIN;
        *out_len    = 0;
        return NULL;
    }

    msg_hdr     = (ProxyMsgHeader *)(buff_addr);
    msg_size    = msg_hdr->payload_len;
    *ret        = FRONTEND_PROXY_PROCESS_OK;
    *out_len    = msg_size + sizeof(ProxyMsgHeader);

    utils_print("*out_len = %d\n", *out_len);

    return buff_addr;
}


/**
 * @brief Initialize the global FrontendEngine instance
 *
 * This function initializes the global FrontendEngine singleton (pointed to by `p_g_fr_eng`),
 * including resource allocation, dependency initialization, and configuration loading.
 * It must be called before any other operations related to the FrontendEngine (e.g., 
 * calling `get_global_frontend_engine()` to use the instance).
 *
 * @note 1. Recommended to call this function once during program startup (e.g., in main() before business logic);
 *       2. This function is not thread-safe. Ensure no concurrent calls during initialization;
 *       3. The function is non-reentrant. Repeated calls may cause resource leaks or initialization conflicts;
 *       4. If initialization fails, subsequent use of the FrontendEngine instance (via `get_global_frontend_engine()`)
 *          may return a null pointer or lead to undefined behavior.
 *
 * @warning Do not skip this initialization or call it multiple times. Uninitialized or repeatedly initialized
 *          FrontendEngine may result in program crashes, resource conflicts, or functional abnormalities.
 */
void frontend_engine_init(){
    int ret;
    memset(&g_fr_eng, 0, sizeof(g_fr_eng));
    p_g_fr_eng = &g_fr_eng;


    p_g_fr_eng->sess_pool = malloc(sizeof(struct FrontendSessionPool));

    ret = frontend_high_speed_init_pool(p_g_fr_eng->sess_pool);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize frontend high-speed session pool (frontend_high_speed_init_pool returned error)!");
        free(p_g_fr_eng->sess_pool);
        abort();
    }

    ret = engine_init_hs_net_dev(p_g_fr_eng);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize frontend high-speed device list!\n!");
        free(p_g_fr_eng->sess_pool);
        free(p_g_fr_eng->dev_set);
        abort();
    }

#if 0
    ret = engine_init_shared_mem_queue(p_g_fr_eng);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize shared memory queue!\n!");
        free(p_g_fr_eng->sess_pool);
        free(p_g_fr_eng->dev_set);
        abort();
    }
#endif

    ret = engine_init_hyperamp_queue(p_g_fr_eng);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize hyperamp memory queue!\n!");
        free(p_g_fr_eng->sess_pool);
        free(p_g_fr_eng->dev_set);
        abort();
    }

    ret = engine_init_iot_devices(p_g_fr_eng);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize iot devices!\n!");
        free(p_g_fr_eng->sess_pool);
        free(p_g_fr_eng->dev_set);
        abort();
    }

    ret = engine_init_iot_sessions(p_g_fr_eng);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_engine_init failed: failed to initialize iot sessions!\n!");
        free(p_g_fr_eng->sess_pool);
        free(p_g_fr_eng->dev_set);
        free(p_g_fr_eng->iot_dev_set);
        abort();
    }

    utils_print("frontend_engine_init success!\n");
}


int frontend_engine_init_eng_ops(FrontendEngine *eng){
    if(NULL == eng){
        error_print("frontend_engine_init_eng_ops failed: the eng point is NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}


int engine_init_shared_mem_queue(FrontendEngine *eng){
    struct SharedMemoryPoolQueue    *rx_queue, *tx_queue;
    SharedMemoryPoolQueueConfig     *rx_queue_conf, *tx_queue_conf;

    if(NULL == eng){
        error_print("engine_init_shared_mem_queue() failed: the engine instance is NULL (uninitialized or invalid)!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    rx_queue_conf   = &high_speed_net_rx_queue_config;
    rx_queue        = shared_mem_pool_queue_create_frontend(rx_queue_conf);


    if(NULL == rx_queue){
        error_print("engine_init_shared_mem_queue() failed: out of memory for the shared memory RX queue allocation!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    tx_queue_conf   = &high_speed_net_tx_queue_config;
    tx_queue        = shared_mem_pool_queue_create_frontend(tx_queue_conf);

    if(NULL == tx_queue){
        error_print("engine_init_shared_mem_queue() failed: out of memory for the shared memory TX queue allocation!");
        free(rx_queue);
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    rx_queue_conf->pool = eng->mem_pool;
    tx_queue_conf->pool = eng->mem_pool;
    
    eng->rx_queue       = rx_queue;
    eng->tx_queue       = tx_queue;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Initialize high-speed network devices for the frontend engine
 *
 * @details This function executes the full initialization process for high-speed network devices.
 *          It validates the input engine pointer, dynamically allocates memory for the high-speed
 *          device set structure, and initializes the global device configuration list.
 *          The function verifies the validity of the device count from the global configuration,
 *          initializes the device set memory to zero, and traverses the global configuration to
 *          copy core device parameters (ID, type, name) to the local high-speed network device array.
 *          All allocated memory will be freed properly in error scenarios to avoid memory leaks.
 *
 * @param eng Pointer to the FrontendEngine instance, must not be NULL
 *
 * @return int Status code indicating the execution result of device initialization
 * @retval FRONTEND_PROXY_PROCESS_OK Initialization completed successfully
 * @retval FRONTEND_PROXY_PROCESS_ERROR Initialization failed (null pointer, memory allocation failure, invalid device count)
 *
 * @note Depends on external function: frontend_init_dev_list()
 * @note Depends on global configuration structure: p_global_dev_list_cfg
 * @note Depends on macro definitions: MAX_HS_DEV_NUM, MIN_HS_DEV_NUM
 * @note Uses memcpy for device name copying and memset for memory zero-initialization
 *
 * @warning The function performs dynamic memory allocation with malloc(), remember to manage
 *          the lifecycle of dev_set in subsequent business logic
 */
int engine_init_hs_net_dev(FrontendEngine *eng){
    FrontendHighSpeedNetDeviceSet    *dev_set;
    FrontendDevInfo                  *dev_info;
    int                              dev_cnt, dev_num;

    if(NULL == eng){
        error_print("engine_init_hs_net_dev failed: the engine should not be NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    dev_set = malloc(sizeof(FrontendHighSpeedNetDeviceSet));

    if(NULL == dev_set){
        error_print("engine_init_hs_net_dev failed: insurficient memory resource!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/*
 * Initialize global device configuration list
 */
    frontend_init_dev_list();

    dev_num = p_global_dev_list_cfg->dev_num;

    if(dev_num > MAX_HS_DEV_NUM || dev_num < MIN_HS_DEV_NUM){
        error_print("engine_init_hs_net_dev failed: invalied device number!\n");
        free(dev_set);
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    memset(dev_set, 0, sizeof(FrontendHighSpeedNetDeviceSet));
    dev_cnt = 0;

/*
 * Traverse and obtain device pointers sequentially based on configurations in p_global_dev_list_cfg.
 */
    while(dev_cnt < dev_num){
        dev_info            = &dev_set->hs_net_dev[dev_cnt];
        dev_info->dev_id    = p_global_dev_list_cfg->dev_info[dev_cnt].dev_id;
        dev_info->dev_type  = p_global_dev_list_cfg->dev_info[dev_cnt].dev_status;
        memcpy(dev_info->name, p_global_dev_list_cfg->dev_info[dev_cnt].name, strlen(p_global_dev_list_cfg->dev_info[dev_cnt].name));
//        utils_print("The device name is %s\n", p_global_dev_list_cfg->dev_info[dev_cnt].name);
//        utils_print("The device name is %s\n", dev_info->name);
        dev_cnt++;
    }

    eng->dev_set = dev_set;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Initializes the Hyperamp shared transmit and receive queues for the frontend engine.
 *
 * This function initializes both the transmit (TX) and receive (RX) shared memory queues
 * used by the Hyperamp inter-OS communication layer. It uses the global configuration
 * structures (e.g., tx_config and rx_config) that specify physical/virtual addresses,
 * capacity, block size, and mapping mode.
 *
 * The initialization includes setting up queue metadata, validating memory mappings,
 * and preparing the queues for message passing between seL4 and Linux.
 *
 * @param[in] eng Pointer to the FrontendEngine instance. Must not be NULL.
 *
 * @return int Status code indicating the result of the initialization:
 *         - @c FRONTEND_PROXY_PROCESS_OK on success.
 *         - @c FRONTEND_PROXY_PROCESS_ERROR if any step fails (e.g., invalid config,
 *           memory mapping error, or queue setup failure).
 *
 * @note This function assumes that the underlying shared memory regions are already
 *       mapped into the virtual address space of the current process.
 */
int engine_init_hyperamp_queue(FrontendEngine *eng){
    int ret;

    if(NULL == eng){
        error_print("engine_init_hyperamp_queue failed: input parameter must not be NULL\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    /*
     * Read shared memory virtual addresses from the IPC buffer msg[] fields.
     * Kernel boot publishes:
     *   - CH0 in msg[2..4]
     *   - CH1 in msg[5..7]
     * NOTE: Do NOT issue any seL4 syscalls before reading these values.
     */
    seL4_Word ch1_tx = seL4_GetMR(5);
    seL4_Word ch1_rx = seL4_GetMR(6);
    seL4_Word ch1_dt = seL4_GetMR(7);
    seL4_Word ch0_tx = seL4_GetMR(2);
    seL4_Word ch0_rx = seL4_GetMR(3);
    seL4_Word ch0_dt = seL4_GetMR(4);

    uint64_t phy_offset = 0;
    uint16_t queue_capacity = HYPERAMP_CH0_QUEUE_CAP;
    if (ch1_tx && ch1_rx && ch1_dt) {
        /* Default: use CH1 for network proxy */
        g_hyper_tx_queue    = (volatile HyperampShmQueue *)ch1_tx;  // TX: seL4 → Linux
        g_hyper_rx_queue    = (volatile HyperampShmQueue *)ch1_rx;  // RX: Linux → seL4
        g_hyper_data_region = (volatile void *)            ch1_dt;  // CH1 Data
        phy_offset = HYPERAMP_CH1_OFFSET_PADDR;
        queue_capacity = HYPERAMP_CH1_QUEUE_CAP;

        printf("[seL4] Shared Memory Addresses (CH1, from IPC msg[5..7]):\n");
    } else {
        /* Fallback: legacy CH0 layout */
        g_hyper_tx_queue    = (volatile HyperampShmQueue *)ch0_tx;
        g_hyper_rx_queue    = (volatile HyperampShmQueue *)ch0_rx;
        g_hyper_data_region = (volatile void *)            ch0_dt;
        phy_offset = 0;
        queue_capacity = HYPERAMP_CH0_QUEUE_CAP;

        printf("[seL4] Shared Memory Addresses (CH0 fallback, from IPC msg[2..4]):\n");
    }

    printf("  TX Queue (seL4->Linux): %p\n", (void *)g_hyper_tx_queue);
    printf("  RX Queue (Linux->seL4): %p\n", (void *)g_hyper_rx_queue);
    printf("  Data Region  :          %p\n", (void *)g_hyper_data_region);

    /* Update queue init configs to match the selected channel */
    hyperamp_tx_config.map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH;
    hyperamp_tx_config.capacity   = queue_capacity;
    hyperamp_tx_config.block_size = 4096;
    hyperamp_tx_config.phy_addr   = (uint64_t)SHM_TX_QUEUE_PADDR + phy_offset;
    hyperamp_tx_config.virt_addr  = (uint64_t)g_hyper_tx_queue;

    hyperamp_rx_config.map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH;
    hyperamp_rx_config.capacity   = queue_capacity;
    hyperamp_rx_config.block_size = 4096;
    hyperamp_rx_config.phy_addr   = (uint64_t)SHM_RX_QUEUE_PADDR + phy_offset;
    hyperamp_rx_config.virt_addr  = (uint64_t)g_hyper_rx_queue;

    ret = hyperamp_queue_init(g_hyper_tx_queue, &hyperamp_tx_config, 1);

    if(HYPERAMP_OK != ret){
        error_print("engine_init_hyperamp_queue failed: failed to initialize Hyperamp TX queue!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/* Critical: Invalidate cache to ensure the printed data is the latest from physical memory */
    hyperamp_cache_invalidate((volatile void *)g_hyper_tx_queue, 64);

    ret = hyperamp_queue_init(g_hyper_rx_queue, &hyperamp_rx_config, 1);

    if(HYPERAMP_OK != ret){
        error_print("engine_init_hyperamp_queue failed: failed to initialize Hyperamp RX queue!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/* Critical: Invalidate cache to ensure subsequent reads reflect the latest data from physical memory */
    hyperamp_cache_invalidate((volatile void *)g_hyper_rx_queue, 64);

    eng->hyper_tx_queue         = g_hyper_tx_queue;
    eng->hyper_rx_queue         = g_hyper_rx_queue;
    eng->hyper_amp_data_region  = g_hyper_data_region;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Main loop function of the engine, handling message processing and data transmission cyclically
 * This function executes a continuous loop consisting of four main steps. After completing all steps,
 * it returns to the first step to implement the core operation of the frontend engine.
 * @details The loop process is as follows:
 *  
 * 1. Read data from the RX queue of the shared memory queue owned by the FrontendEngine instance, and process 
 * them sequentially through the frontend proxy protocol stack.
 * If any data is read, there are two cases:
 * (a) For device messages, strategy messages, and session messages (these are responses from the backend):
 * The frontend proxy protocol stack performs corresponding processing for each type, and updates the frontend's 
 * relevant state based on the message content (the frontend does not generate response packets).
 * (b) For sessions that receive data messages:
 * These sessions are placed into the backend-to-frontend active queue for processing in step (2).
 * If no data message is found, the procedure jumps to step (3).
 * 
 * 2. Access and process session instances in the backend-to-frontend active queue sequentially:
 * Sequentially call the application callback function registered by each session in the queue (serving as a data-ready notification).
 * After receiving the callback notification, the application will call the dedicated read API to read data from the corresponding session,
 * and complete business processing independently based on the read data (the frontend does not involve active data extraction or socket transmission here).
 * If the application needs to send data to the backend proxy through the shared memory queue, when the data to be sent is added to the send buffer,
 * the session will be added to the frontend-to-backend active queue, and these sessions will be processed in STEP (3).
 * 
 * 3. Access and process session instances in the frontend-to-backend active queue sequentially:
 * For each session in the queue, read the pending data stored in the session's send buffer (data to be sent to the backend proxy by the application).
 * Construct data messages according to the frontend proxy protocol specification, and send the messages to the backend proxy through the shared memory's 
 * TX queue.
 * After the data is successfully sent, clear the corresponding pending data in the session's send buffer; if the send buffer becomes empty, remove the session 
 * from the frontend-to-backend active queue. (This step specifically handles data transmission requests initiated by the application, realizing the frontend-
 * -to-backend data proxy through the shared memory queue)
 * 
 * After completing all operations in step (3), the loop returns to step (1) to continue the cyclic processing.
 */
void frontend_engine_run(){
    FrontendEngine                   *eng;
    struct SharedMemoryPoolQueue     *rx_queue, *tx_queue;
    struct FrontendSessionQueue      *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                          *proxy_msg;
    uint32_t                         msg_size;
    int                              ret;

    eng = frontend_get_global_engine();
/* 
 * rx_queue: Local receive queue, which actually maps to the back-end's transmit queue (tx_queue).
 * Data sent by the front-end through its tx_queue will be received by the local side via this rx_queue.
 * 
 * tx_queue: Local transmit queue, which is used as the back-end's receive queue (rx_queue).
 * Data sent by the local side through this tx_queue will be received by the back-end via its rx_queue.
 */
    if(NULL == eng->rx_queue || NULL == eng->tx_queue){
        error_print("frontend_engine_run failed: The global backend engine's RX queue or TX queue has not been initialized!");
        return ;
    }

    FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);
    FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);

    if(NULL == active_queue_f2b || NULL == active_queue_b2f){
        error_print("frontend_engine_run failed: The global frontend engine's f2b session queue or b2f session queue has not been initialized!");
        return ;
    }

    if(NULL == eng->sess_pool || NULL == eng->sess_pool->ops){
        error_print("frontend_engine_run failed: Global frontend engine's session pool (sess_pool) or its operation set (ops) is not initialized!");
        return ;
    }

    sess_pool       = eng->sess_pool;
    sess_pool_ops   = sess_pool->ops;

    do{
/*
 * STEP (1).
 */
eng_run_step1:
/* 
 * Acquire access lock for the RX queue.
 */
        ret = SHARED_MEM_QUEUE_LOCK(rx_queue);

/* 
 * If returning BACKEND_PROXY_PROCESS_ERROR, it indicates a system-level error (e.g., invalid lock handle, shared memory pool corruption)
 * Failed to acquire the lock; print error message and exit the current flow.
 */
        if(FRONTEND_PROXY_PROCESS_ERROR == ret){
            error_print("frontend_engine_run failed: failed to get the lock of the RX queue!");
            return;
        }

/* 
 * If returning BACKEND_PROXY_PROCESS_AGAIN, it indicates lock acquisition timed out (temporary unavailability, e.g., lock held by another process)
 * No error occurred; jump to eng_run_step3 to retry or proceed with alternative logic.
 */
        if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
            goto eng_run_step3;
        }


        do{

    /*
     * Retrieve data from the RX queue.
     */
            ret = frontend_engine_rx_queue_get(rx_queue, &proxy_msg, PROXY_MSG_HDR_PLUS_MAX_SIZE, &msg_size);

    /*
     * If returning FRONTEND_PROXY_PROCESS_ERROR, it indicates a system-level error (e.g., invalid queue handle, shared memory access exception, etc.)
     * Processing cannot continue; print error message and return directly.
     */
            if(FRONTEND_PROXY_PROCESS_ERROR == ret){
                error_print("frontend_engine_run failed: failed to get data from RX queue!");
                return;
            }

    /*
     * If returning FRONTEND_PROXY_PROCESS_AGAIN, it indicates temporary inability to retrieve data (e.g., empty queue, resource temporarily occupied, etc., non-error state)     
     * No error reporting needed; jump to eng_run_step2 to execute the next process.
     */
            if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
                goto eng_run_step2;
            }
    /*
     * Process the proxy message.
     */
            frontend_proxy_msg_process(proxy_msg);

        }while(FRONTEND_PROXY_PROCESS_OK == ret);

eng_run_step2:
        SHARED_MEM_QUEUE_UNLOCK(rx_queue);
/*
 * Recall the FRONTEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (1) procedure may renew the back-to-front queue (queue_b2f) of the session pool.
 */
        FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);

        TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess){
            cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_RECVDATA);

/*
 * queue_b2f: Session queue for sessions with pending data to be received by the frontend from the backend.
 * (Pending data refers to data sent from the backend to the frontend, which the frontend needs to read.)
 * If the receive queue (msg_b2f) is empty, the session instance should be removed from the queue_b2f list.
 */
            if(TAILQ_EMPTY(&cur_sess->msg_b2f)){
                TAILQ_REMOVE(active_queue_b2f, cur_sess, entries_b2f);
                cur_sess->state_b2f &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
            }
        } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess)


eng_run_step3:
/*
 * Recall the BACKEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (2) procedure may renew the front-to-back queue (queue_f2b) of the session pool.
 */
        FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);

        SHARED_MEM_QUEUE_LOCK(tx_queue);

        TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess){

/*
 * Call the corresponding data_process_f2b function pointer in the session pool's operation set (sess_pool_ops), which attempts to send the frontend-to-backend data maintained by the
 * current session (cur_sess) via the shared memory's TX queue.
 * The return value corresponds to three scenarios:
 * Returns FRONTEND_PROXY_PROCESS_OK: All data has been sent successfully.
 * Returns FRONTEND_PROXY_PROCESS_AGAIN: Not all data has been sent, and no errors occurred.
 * Returns FRONTEND_PROXY_PROCESS_ERROR: An error occurred during the sending process.
 */
            ret = sess_pool_ops->data_process_f2b(cur_sess);
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_OK, this indicates all message segments in the front-to-back (F2B) message queue have been sent via the shared memory's TX queue. 
 * Such sessions should be detached from the F2B active queue, and their "linked to queue" state flag should be cleared.
 */
            if(FRONTEND_PROXY_PROCESS_OK == ret){
                TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
                cur_sess->state_f2b &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
            }
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_ERROR, it indicates an error occurred when attempting to send data via the shared memory's TX queue.
 * For such sessions, they should be removed from the frontend-to-backend active queue (active_queue_f2b), and the session's abnormal event callback should be triggered.
 * 
 * Note that the recovery of resources occupied by the session needs to be completed during the processing of the FRONTEND_SESS_EVENT_ABNORMAL event, and this responsibility 
 * lies with the application.
 */
            if(FRONTEND_PROXY_PROCESS_ERROR == ret){
                TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
                cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_ABNORMAL);
            }
/*
 * When not all data has been sent and no errors have occurred, no action is required.
 */
        } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess)

        SHARED_MEM_QUEUE_UNLOCK(tx_queue);

    }while(1);
}


/**
 * @brief Main loop function of the engine (HyperAMP version), handling message processing and data transmission cyclically
 * This function executes a continuous loop consisting of four main steps, identical in logic to frontend_engine_run, 
 * with the key difference being that all inter-environment communication is performed via the HyperAMP queue (instead of 
 * the standard shared memory queue) to interact with the backend running on Linux. After completing all steps,
 * it returns to the first step to implement the core operation of the frontend engine.
 * @details The loop process is as follows:
 *  
 * 1. Read data from the RX queue of the HyperAMP queue owned by the FrontendEngine instance, and process 
 * them sequentially through the frontend proxy protocol stack.
 * If any data is read, there are two cases:
 * (a) For device messages, strategy messages, and session messages (these are responses from the Linux backend):
 * The frontend proxy protocol stack performs corresponding processing for each type, and updates the frontend's 
 * relevant state based on the message content (the frontend does not generate response packets).
 * (b) For sessions that receive data messages:
 * These sessions are placed into the backend-to-frontend active queue for processing in step (2).
 * If no data message is found, the procedure jumps to step (3).
 * 
 * 2. Access and process session instances in the backend-to-frontend active queue sequentially:
 * Sequentially call the application callback function registered by each session in the queue (serving as a data-ready notification).
 * After receiving the callback notification, the application will call the dedicated read API to read data from the corresponding session,
 * and complete business processing independently based on the read data (the frontend does not involve active data extraction or socket transmission here).
 * If the application needs to send data to the Linux backend proxy through the HyperAMP queue, when the data to be sent is added to the send buffer,
 * the session will be added to the frontend-to-backend active queue, and these sessions will be processed in STEP (3).
 * 
 * 3. Access and process session instances in the frontend-to-backend active queue sequentially:
 * For each session in the queue, read the pending data stored in the session's send buffer (data to be sent to the Linux backend proxy by the application).
 * Construct data messages according to the frontend proxy protocol specification, and send the messages to the Linux backend proxy through the HyperAMP's 
 * TX queue.
 * After the data is successfully sent, clear the corresponding pending data in the session's send buffer; if the send buffer becomes empty, remove the session 
 * from the frontend-to-backend active queue. (This step specifically handles data transmission requests initiated by the application, realizing the frontend-
 * -to-backend data proxy through the HyperAMP queue)
 * 
 * After completing all operations in step (3), the loop returns to step (1) to continue the cyclic processing.
 */
void frontend_engine_run_hyperamp(){
    FrontendEngine                   *eng;
    volatile HyperampShmQueue        *hyper_rx_queue, hyper_tx_queue;
    struct FrontendSessionQueue      *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                          *proxy_msg;
    uint32_t                         msg_size;
    int                              ret;
    size_t                           block_size;
    uint8_t                          msg_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];

    eng = frontend_get_global_engine();

/* 
 * hyper_rx_queue: HyperAMP receive queue instance for cross-OS shared memory communication.
 * This local receive queue maps to the front-end's HyperAMP transmit queue (hyper_tx_queue).
 * Data sent by the front-end through its hyper_tx_queue is received locally via this hyper_rx_queue.
 * 
 * hyper_tx_queue: HyperAMP transmit queue instance for cross-OS shared memory communication.
 * This local transmit queue serves as the front-end's HyperAMP receive queue (hyper_rx_queue).
 * Data sent locally through this hyper_tx_queue is received by the front-end via its hyper_rx_queue.
 * 
 * hyper_amp_data_region: The memory region where cross-OS shared memory data is stored,
 * which is the underlying storage for data transmitted via HyperAMP queues.
 */

    if(NULL == eng->hyper_rx_queue || NULL == eng->hyper_tx_queue){
        error_print("frontend_engine_run failed: The global backend engine's RX queue or TX queue has not been initialized!");
        return ;
    }

    FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);
    FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);

    if(NULL == active_queue_f2b || NULL == active_queue_b2f){
        error_print("frontend_engine_run failed: The global frontend engine's f2b session queue or b2f session queue has not been initialized!");
        return ;
    }

    if(NULL == eng->sess_pool || NULL == eng->sess_pool->ops){
        error_print("frontend_engine_run failed: Global frontend engine's session pool (sess_pool) or its operation set (ops) is not initialized!");
        return ;
    }

    sess_pool       = eng->sess_pool;
    sess_pool_ops   = sess_pool->ops;

    do{
/*
 * STEP (1).
 */
eng_run_step1:
        do{
            ps_sdelay(1);
            printf("In %s-1\n", __func__);
            ret = frontend_engine_hyperamp_rx_queue_get(eng, HYPERAMP_MSG_HDR_PLUS_MAX_SIZE, msg_buf, &block_size);

    /*
     * If returning FRONTEND_PROXY_PROCESS_ERROR, it indicates a system-level error (e.g., invalid queue handle, shared memory access exception, etc.)
     * Processing cannot continue; print error message and return directly.
     */
            if(FRONTEND_PROXY_PROCESS_ERROR == ret){
                error_print("frontend_engine_run failed: failed to get data from RX queue!");
                return;
            }

    /*
     * If returning FRONTEND_PROXY_PROCESS_AGAIN, it indicates temporary inability to retrieve data (e.g., empty queue, resource temporarily occupied, etc., non-error state)     
     * No error reporting needed; jump to eng_run_step2 to execute the next process.
     */     printf("In %s-2\n", __func__);
            if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
                goto eng_run_step2;
            }
            printf("In %s-3\n", __func__);
    /*
     * Process the proxy message.
     */
            frontend_proxy_msg_process(msg_buf);
            printf("In %s-4\n", __func__);
        }while(FRONTEND_PROXY_PROCESS_OK  == ret);

eng_run_step2:
/*
 * Recall the FRONTEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (1) procedure may renew the back-to-front queue (queue_b2f) of the session pool.
 */     printf("In %s-5\n", __func__);
        FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);
        printf("In %s-6\n", __func__);
        TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess){
            printf("In %s-7\n", __func__);
            cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_RECVDATA);

/*
 * queue_b2f: Session queue for sessions with pending data to be received by the frontend from the backend.
 * (Pending data refers to data sent from the backend to the frontend, which the frontend needs to read.)
 * If the receive queue (msg_b2f) is empty, the session instance should be removed from the queue_b2f list.
 */
            if(TAILQ_EMPTY(&cur_sess->msg_b2f)){
                TAILQ_REMOVE(active_queue_b2f, cur_sess, entries_b2f);
                cur_sess->state_b2f &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
            }
            printf("In %s-8\n", __func__);
        } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess)



eng_run_step3:
/*
 * Recall the FRONTEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (2) procedure may renew the front-to-back queue (queue_f2b) of the session pool.
 */     printf("In %s-9\n", __func__);
        FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);
        printf("In %s-10\n", __func__);
        TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess){

/*
 * Call the corresponding data_process_f2b function pointer in the session pool's operation set (sess_pool_ops), which attempts to send the frontend-to-backend data maintained by the
 * current session (cur_sess) via the shared memory's TX queue.
 * The return value corresponds to three scenarios:
 * Returns FRONTEND_PROXY_PROCESS_OK: All data has been sent successfully.
 * Returns FRONTEND_PROXY_PROCESS_AGAIN: Not all data has been sent, and no errors occurred.
 * Returns FRONTEND_PROXY_PROCESS_ERROR: An error occurred during the sending process.
 */         printf("In %s-11\n", __func__);
            ret = sess_pool_ops->data_process_f2b(cur_sess);
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_OK, this indicates all message segments in the front-to-back (F2B) message queue have been sent via the shared memory's TX queue. 
 * Such sessions should be detached from the F2B active queue, and their "linked to queue" state flag should be cleared.
 */         printf("In %s-12\n", __func__);
            if(FRONTEND_PROXY_PROCESS_OK == ret){
                printf("In %s-13\n", __func__);
                TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
                cur_sess->state_f2b &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
            }
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_ERROR, it indicates an error occurred when attempting to send data via the shared memory's TX queue.
 * For such sessions, they should be removed from the frontend-to-backend active queue (active_queue_f2b), and the session's abnormal event callback should be triggered.
 * 
 * Note that the recovery of resources occupied by the session needs to be completed during the processing of the FRONTEND_SESS_EVENT_ABNORMAL event, and this responsibility 
 * lies with the application.
 */
            if(FRONTEND_PROXY_PROCESS_ERROR == ret){
                printf("In %s-14\n", __func__);
                TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
                cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_ABNORMAL);
            }
            printf("In %s-15\n", __func__);
/*          
 * When not all data has been sent and no errors have occurred, no action is required.
 */
        } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess)
    }while(1);

}


/**
 * @brief Single-execution function of the engine (HyperAMP version), handling message processing and data transmission once
 * This function executes the core logic of the frontend engine once (instead of a continuous loop), which is consistent with the logic of frontend_engine_run_hyperamp,
 * with the key difference being that it only completes one round of processing and then returns, rather than looping continuously. All inter-environment communication
 * is still performed via the HyperAMP queue to interact with the backend running on Linux.
 * @details The single execution process is as follows:
 *  
 * 1. Read data from the RX queue of the HyperAMP queue owned by the FrontendEngine instance, and process 
 * them sequentially through the frontend proxy protocol stack.
 * If any data is read, there are two cases:
 * (a) For device messages, strategy messages, and session messages (these are responses from the Linux backend):
 * The frontend proxy protocol stack performs corresponding processing for each type, and updates the frontend's 
 * relevant state based on the message content (the frontend does not generate response packets).
 * (b) For sessions that receive data messages:
 * These sessions are placed into the backend-to-frontend active queue for processing in step (2).
 * If no data message is found, the procedure jumps to step (3).
 *  
 * 2. Access and process session instances in the backend-to-frontend active queue sequentially:
 * Sequentially call the application callback function registered by each session in the queue (serving as a data-ready notification).
 * After receiving the callback notification, the application will call the dedicated read API to read data from the corresponding session,
 * and complete business processing independently based on the read data (the frontend does not involve active data extraction or socket transmission here).
 * If the application needs to send data to the Linux backend proxy through the HyperAMP queue, when the data to be sent is added to the send buffer,
 * the session will be added to the frontend-to-backend active queue, and these sessions will be processed in STEP (3).
 * 
 * 3. Access and process session instances in the frontend-to-backend active queue sequentially:
 * For each session in the queue, read the pending data stored in the session's send buffer (data to be sent to the Linux backend proxy by the application).
 * Construct data messages according to the frontend proxy protocol specification, and send the messages to the Linux backend proxy through the HyperAMP's 
 * TX queue.
 * After the data is successfully sent, clear the corresponding pending data in the session's send buffer; if the send buffer becomes empty, remove the session 
 * from the frontend-to-backend active queue. (This step specifically handles data transmission requests initiated by the application, realizing the frontend-
 * -to-backend data proxy through the HyperAMP queue)
 *  
 * After completing all operations in step (3), the function returns directly without looping back to step (1), completing a single round of engine processing.
 * @note 1. This function is a single-execution version of frontend_engine_run_hyperamp, suitable for scenarios where periodic or on-demand single processing is required;
 *       2. The processing logic of each step is completely consistent with frontend_engine_run_hyperamp, only the cyclic execution is removed;
 *       3. All inter-environment communication still relies on the HyperAMP queue, maintaining compatibility with the Linux backend.
 */
void frontend_engine_run_hyperamp_once(){
    FrontendEngine                   *eng;
    volatile HyperampShmQueue        *hyper_rx_queue, *hyper_tx_queue;
    struct FrontendSessionQueue      *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                          *proxy_msg;
    uint32_t                         msg_size;
    int                              ret;
    size_t                           block_size;
    uint8_t                          msg_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];

    eng = frontend_get_global_engine();

/* 
 * hyper_rx_queue: HyperAMP receive queue instance for cross-OS shared memory communication.
 * This local receive queue maps to the front-end's HyperAMP transmit queue (hyper_tx_queue).
 * Data sent by the front-end through its hyper_tx_queue is received locally via this hyper_rx_queue.
 * 
 * hyper_tx_queue: HyperAMP transmit queue instance for cross-OS shared memory communication.
 * This local transmit queue serves as the front-end's HyperAMP receive queue (hyper_rx_queue).
 * Data sent locally through this hyper_tx_queue is received by the front-end via its hyper_rx_queue.
 * 
 * hyper_amp_data_region: The memory region where cross-OS shared memory data is stored,
 * which is the underlying storage for data transmitted via HyperAMP queues.
 */

    if(NULL == eng->hyper_rx_queue || NULL == eng->hyper_tx_queue){
        error_print("frontend_engine_run_hyperamp_once failed: The global backend engine's RX queue or TX queue has not been initialized!");
        return ;
    }

    FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);
    FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);

    if(NULL == active_queue_f2b || NULL == active_queue_b2f){
        error_print("frontend_engine_run_hyperamp_once failed: The global frontend engine's f2b session queue or b2f session queue has not been initialized!");
        return ;
    }

    if(NULL == eng->sess_pool || NULL == eng->sess_pool->ops){
        error_print("frontend_engine_run_hyperamp_once failed: Global frontend engine's session pool (sess_pool) or its operation set (ops) is not initialized!");
        return ;
    }

    sess_pool       = eng->sess_pool;
    sess_pool_ops   = sess_pool->ops;

/*
 * Remove the do-while(1) loop of the original function, execute only one round of the three-step processing flow, and return directly after completion
 * STEP (1): Read data from HyperAMP RX queue and process it
 */
eng_run_step1:
    do{
        ps_sdelay(1);
        printf("In %s-1\n", __func__);
        ret = frontend_engine_hyperamp_rx_queue_get(eng, HYPERAMP_MSG_HDR_PLUS_MAX_SIZE, msg_buf, &block_size);

    /*
     * If returning FRONTEND_PROXY_PROCESS_ERROR, it indicates a system-level error (e.g., invalid queue handle, shared memory access exception, etc.)
     * Processing cannot continue; print error message and return directly.
     */
        if(FRONTEND_PROXY_PROCESS_ERROR == ret){
            error_print("frontend_engine_run_hyperamp_once failed: failed to get data from RX queue!");
            return;
        }

    /*
     * If returning FRONTEND_PROXY_PROCESS_AGAIN, it indicates temporary inability to retrieve data (e.g., empty queue, resource temporarily occupied, etc., non-error state)     
     * No error reporting needed; jump to eng_run_step2 to execute the next process.
     */     
        printf("In %s-2\n", __func__);
        if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
            goto eng_run_step2;
        }
        printf("In %s-3\n", __func__);
    /*
     * Process the proxy message.
     */
        frontend_proxy_msg_process(msg_buf);
        printf("In %s-4\n", __func__);
    }while(FRONTEND_PROXY_PROCESS_OK  == ret);

eng_run_step2:
/*
 * Recall the FRONTEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (1) procedure may renew the back-to-front queue (queue_b2f) of the session pool.
 */     
    printf("In %s-5\n", __func__);
    FRONTEND_ENGINE_GET_B2F_QUEUE(eng, active_queue_b2f);
    printf("In %s-6\n", __func__);
    TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess){
        printf("In %s-7\n", __func__);
        cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_RECVDATA);

/*
 * queue_b2f: Session queue for sessions with pending data to be received by the frontend from the backend.
 * (Pending data refers to data sent from the backend to the frontend, which the frontend needs to read.)
 * If the receive queue (msg_b2f) is empty, the session instance should be removed from the queue_b2f list.
 */
        if(TAILQ_EMPTY(&cur_sess->msg_b2f)){
            TAILQ_REMOVE(active_queue_b2f, cur_sess, entries_b2f);
            cur_sess->state_b2f &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
        }
        printf("In %s-8\n", __func__);
    } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_b2f, entries_b2f, next_sess)

eng_run_step3:
/*
 * Recall the FRONTEND_ENGINE_GET_F2B_QUEUE again to update active_queue_b2f, because the STEP (2) procedure may renew the front-to-back queue (queue_f2b) of the session pool.
 */     
    printf("In %s-9\n", __func__);
    FRONTEND_ENGINE_GET_F2B_QUEUE(eng, active_queue_f2b);
    printf("In %s-10\n", __func__);
    TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess){

/*
 * Call the corresponding data_process_f2b function pointer in the session pool's operation set (sess_pool_ops), which attempts to send the frontend-to-backend data maintained by the
 * current session (cur_sess) via the shared memory's TX queue.
 * The return value corresponds to three scenarios:
 * Returns FRONTEND_PROXY_PROCESS_OK: All data has been sent successfully.
 * Returns FRONTEND_PROXY_PROCESS_AGAIN: Not all data has been sent, and no errors occurred.
 * Returns FRONTEND_PROXY_PROCESS_ERROR: An error occurred during the sending process.
 */         
        printf("In %s-11\n", __func__);
        ret = sess_pool_ops->data_process_f2b(cur_sess);
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_OK, this indicates all message segments in the front-to-back (F2B) message queue have been sent via the shared memory's TX queue. 
 * Such sessions should be detached from the F2B active queue, and their "linked to queue" state flag should be cleared.
 */         
        printf("In %s-12\n", __func__);
        if(FRONTEND_PROXY_PROCESS_OK == ret){
            printf("In %s-13\n", __func__);
            TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
            cur_sess->state_f2b &= ~FRONTEND_SESS_LINKED_TO_QUEUE;
        }
/*
 * If data_process_f2b returns FRONTEND_PROXY_PROCESS_ERROR, it indicates an error occurred when attempting to send data via the shared memory's TX queue.
 * For such sessions, they should be removed from the frontend-to-backend active queue (active_queue_f2b), and the session's abnormal event callback should be triggered.
 * 
 * Note that the recovery of resources occupied by the session needs to be completed during the processing of the FRONTEND_SESS_EVENT_ABNORMAL event, and this responsibility 
 * lies with the application.
 */
        if(FRONTEND_PROXY_PROCESS_ERROR == ret){
            printf("In %s-14\n", __func__);
            TAILQ_REMOVE(active_queue_f2b, cur_sess, entries_f2b);
            cur_sess->event_callback(cur_sess, FRONTEND_SESS_EVENT_ABNORMAL);
        }
        printf("In %s-15\n", __func__);
/*          
 * When not all data has been sent and no errors have occurred, no action is required.
 */
    } // TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess)

    // Return directly after completing all operations in the three steps without looping
    return;
}


/**
 * @brief Initialize IoT devices.
 * @param engine Pointer to FrontendEngine instance.
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): All devices initialized successfully OR no devices to parse
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Critical error (invalid input/INI load failed/no devices parsed)
 * 
 * @warning Ensure MAX_IOT_DEV_NUM, MAX_DEV_NAME are defined before using this function
 * @warning Call frontend_engine_cleanup_iot_devices() to free allocated memory
 */
int engine_init_iot_devices(FrontendEngine *engine){
    // 1. Validate input parameters
    IotDevice *dev;
    if (engine == NULL) {
        fprintf(stderr, "[ERROR] Invalid BackendEngine pointer (NULL)\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

// 2. Initialize IoT device set (allocate memory if not exists)
    if (engine->iot_dev_set == NULL) {
        engine->iot_dev_set = (FrontendIoTDeviceSet *)calloc(1, sizeof(FrontendIoTDeviceSet));
        if (engine->iot_dev_set == NULL) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for IoTDeviceSet\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
        memset(engine->iot_dev_set, 0, sizeof(FrontendIoTDeviceSet));
    }

    dev             = &engine->iot_dev_set->iot_dev[0];
    dev->dev_type   = IOT_PROTO_TYPE_BLUETOOTH;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    dev             = &engine->iot_dev_set->iot_dev[1];
    dev->dev_type   = IOT_PROTO_TYPE_CAN;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    dev             = &engine->iot_dev_set->iot_dev[2];
    dev->dev_type   = IOT_PROTO_TYPE_ZIGBEE;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    dev             = &engine->iot_dev_set->iot_dev[3];
    dev->dev_type   = IOT_PROTO_TYPE_LORA;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    dev             = &engine->iot_dev_set->iot_dev[4];
    dev->dev_type   = IOT_PROTO_TYPE_POWERLINK;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    dev             = &engine->iot_dev_set->iot_dev[5];
    dev->dev_type   = IOT_PROTO_TYPE_MODBUSTCP;
    dev->dev_status = IOT_DEV_STATUS_ONLINE;

    engine->iot_dev_num = 6;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize IoT device sessions.
 * @param engine Pointer to FrontendEngine instance
 * @return int Execution result
 *         - BACKEND_PROXY_PROCESS_OK (0): All sessions initialized successfully OR no sessions to start
 *         - BACKEND_PROXY_PROCESS_ERROR (-1): Critical error (invalid input/session creation failed)
 * 
 * @note This function:
 *       1. Iterates through all valid IoT devices in engine->iot_dev_set
 *       2. Starts the corresponding session instance for each device sequentially
 *       3. Initializes protocol-specific communication sessions for connected devices
 *       4. Maintains session context for subsequent data interaction and control
 *       5. **ONLY ONE SESSION PER PROTOCOL TYPE IS SUPPORTED** (Bluetooth/CAN/ZigBee/LoRa/PowerLink/ModbusTCP)
 * 
 * @warning Call backend_iot_sess_destroy() to release session resources
 * @warning Do NOT call this function repeatedly without proper cleanup
 * @warning **Only one instance per IoT protocol type is supported in system**
 */
int engine_init_iot_sessions(FrontendEngine *engine){
    utils_print("In %s\n", __func__);
    IotDevice   *iot_dev;
    int         iot_dev_cnt, iot_dev_num, ret;

    iot_dev_cnt = 0;
    iot_dev_num = engine->iot_dev_num;

    /* Initialize global IoT session handles to NULL */
    frontend_bluetooth_sess  = NULL;
    frontend_can_sess        = NULL;
    frontend_zigbee_sess     = NULL;
    frontend_lora_sess       = NULL;
    frontend_powerlink_sess  = NULL;
    frontend_modbustcp_sess  = NULL;

    /* Check for invalid input pointers */
    if (NULL == engine || NULL == engine->iot_dev_set) {
        error_print("engine_init_iot_sessions failed: invalid engine pointer!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("iot device number = %d\n", iot_dev_num);

    while(iot_dev_cnt < iot_dev_num){
        iot_dev = &engine->iot_dev_set->iot_dev[iot_dev_cnt];
        if(IOT_DEV_STATUS_ONLINE == iot_dev->dev_status){
            switch (iot_dev->dev_type) {
                case IOT_PROTO_TYPE_BLUETOOTH:
                    /* Ensure only ONE Bluetooth device instance is supported */
                    if(NULL != frontend_bluetooth_sess){
                        error_print("engine_init_iot_sessions failed: only one bluetooth device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_bluetooth_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_bluetooth_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_bluetooth_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_bluetooth_session(iot_dev, frontend_bluetooth_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_bluetooth_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_bluetooth_sess->eng = engine;

                    break;
                case IOT_PROTO_TYPE_CAN:
                    /* Ensure only ONE CAN device instance is supported */
                    if(NULL != frontend_can_sess){
                        error_print("engine_init_iot_sessions failed: only one CAN device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_can_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_can_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_can_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_can_session(iot_dev, frontend_can_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_can_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_can_sess->eng = engine;

                    break;
                case IOT_PROTO_TYPE_ZIGBEE:
                    /* Ensure only ONE ZigBee device instance is supported */
                    if(NULL != frontend_zigbee_sess){
                        error_print("engine_init_iot_sessions failed: only one ZigBee device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_zigbee_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_zigbee_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_zigbee_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_zigbee_session(iot_dev, frontend_zigbee_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_zigbee_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_zigbee_sess->eng = engine;

                    break;
                case IOT_PROTO_TYPE_LORA:
                    /* Ensure only ONE LoRa device instance is supported */
                    if(NULL != frontend_lora_sess){
                        error_print("engine_init_iot_sessions failed: only one LoRa device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_lora_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_lora_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_lora_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_lora_session(iot_dev, frontend_lora_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_lora_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_lora_sess->eng = engine;

                    break;
                case IOT_PROTO_TYPE_POWERLINK:
                    /* Ensure only ONE PowerLink device instance is supported */
                    if(NULL != frontend_powerlink_sess){
                        error_print("engine_init_iot_sessions failed: only one PowerLink device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_powerlink_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_powerlink_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_powerlink_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_powerlink_session(iot_dev, frontend_powerlink_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_powerlink_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_powerlink_sess->eng = engine;

                    break;
                case IOT_PROTO_TYPE_MODBUSTCP:
                    /* Ensure only ONE PowerLink device instance is supported */
                    if(NULL != frontend_modbustcp_sess){
                        error_print("engine_init_iot_sessions failed: only one modbusTCP device is supported in frontendengine!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_modbustcp_sess = malloc(sizeof(IoTFrontendSession));

                    if(NULL == frontend_modbustcp_sess){
                        error_print("engine_init_iot_sessions failed: insufficient memory for allocating frontend_modbustcp_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    ret = engine_init_modbustcp_session(iot_dev, frontend_modbustcp_sess);

                    if(FRONTEND_PROXY_PROCESS_OK != ret){
                        error_print("engine_init_iot_sessions failed: failed to initialize frontend_modbustcp_sess instance!\n");
                        goto init_iot_sess_error;
                    }

                    frontend_modbustcp_sess->eng = engine;

                    break;
                default:
                    error_print("engine_init_iot_sessions failed: unsupported device type!\n");
                    goto init_iot_sess_error;

            } // switch (iot_dev_type)
        } // if(IOT_DEV_STATUS_ONLINE == iot_dev->dev_status)
        iot_dev_cnt++;
    }


    return FRONTEND_PROXY_PROCESS_OK;

init_iot_sess_error:
    /* Free all allocated session resources on error */
    if(NULL != frontend_bluetooth_sess){
        free(frontend_bluetooth_sess);
        frontend_bluetooth_sess = NULL;
    }
    
    if(NULL != frontend_can_sess){
        free(frontend_can_sess);
        frontend_can_sess = NULL;
    }

    if(NULL != frontend_zigbee_sess){
        free(frontend_zigbee_sess);
        frontend_zigbee_sess = NULL;
    }

    if(NULL != frontend_lora_sess){
        free(frontend_lora_sess);
        frontend_lora_sess = NULL;
    }

    if(NULL != frontend_powerlink_sess){
        free(frontend_powerlink_sess);
        frontend_powerlink_sess = NULL;
    }

    if(NULL != frontend_modbustcp_sess){
        free(frontend_modbustcp_sess);
        frontend_modbustcp_sess = NULL;
    }

    return FRONTEND_PROXY_PROCESS_ERROR;

}


/**
 * @brief Initialize Bluetooth communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds session context to the corresponding Bluetooth device
 *       2. Configures session state and communication parameters
 *       3. Sets up data transceiving logic for the session
 *       4. Prepares session for subsequent data interaction
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_bluetooth_session(IotDevice *dev, IoTFrontendSession *sess){
    if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = bluetooth_send_to_backend;
    sess->recv_from_backend     = bluetooth_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_BLUETOOTH;
    sess->event_callback        = default_bluetooth_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize CAN bus communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds session context to the corresponding CAN device
 *       2. Configures session state and bus communication parameters
 *       3. Sets up message queue and data processing logic
 *       4. Prepares session for subsequent bus communication
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_can_session(IotDevice *dev, IoTFrontendSession *sess){
    if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = can_send_to_backend;
    sess->recv_from_backend     = can_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_CAN;
    sess->event_callback        = default_can_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize ZigBee communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds session context to the corresponding ZigBee device
 *       2. Configures session state and network communication parameters
 *       3. Sets up wireless data interaction logic
 *       4. Prepares session for subsequent network communication
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_zigbee_session(IotDevice *dev, IoTFrontendSession *sess){
if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = zigbee_send_to_backend;
    sess->recv_from_backend     = zigbee_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_ZIGBEE;
    sess->event_callback        = default_zigbee_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize LoRa communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds session context to the corresponding LoRa device
 *       2. Configures session state and long-range communication parameters
 *       3. Sets up uplink/downlink data transfer logic
 *       4. Prepares session for subsequent RF communication
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_lora_session(IotDevice *dev, IoTFrontendSession *sess){
    if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = lora_send_to_backend;
    sess->recv_from_backend     = lora_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_LORA;
    sess->event_callback        = default_lora_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize PowerLink real-time communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds real-time session context to the corresponding PowerLink device
 *       2. Configures session state and synchronous communication parameters
 *       3. Sets up real-time data buffer and processing logic
 *       4. Prepares session for subsequent industrial Ethernet communication
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_powerlink_session(IotDevice *dev, IoTFrontendSession *sess){
    if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = powerlink_send_to_backend;
    sess->recv_from_backend     = powerlink_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_POWERLINK;
    sess->event_callback        = default_powerlink_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief Initialize ModbusTCP communication session for IoT device
 * @param dev Pointer to IotDevice instance (pre-initialized device)
 * @param sess Pointer to IoTFrontendSession instance (memory pre-allocated)
 * @return int Execution result
 *         - FRONTEND_PROXY_PROCESS_OK (0): Session initialized successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR (-1): Session initialization failed
 *
 * @note This function only initializes session context, NOT device hardware.
 *       Device initialization is completed in the prior stage.
 *       The memory of IoTFrontendSession is pre-allocated, no need to manage.
 *       1. Binds session context to the corresponding ModbusTCP device
 *       2. Configures session state and TCP communication parameters
 *       3. Sets up data buffer and Modbus frame processing logic
 *       4. Prepares session for subsequent ModbusTCP communication
 *
 * @warning Device hardware initialization must be completed before calling
 * @warning Session memory is managed externally, do not free in this function
 */
int engine_init_modbustcp_session(IotDevice *dev, IoTFrontendSession *sess){
    if(NULL == dev || NULL == sess){
        error_print("engine_init_bluetooth_session failed: invalid input parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->eng                   = frontend_get_global_engine();
    sess->bound_dev             = dev;
    sess->send_to_backend       = modbustcp_send_to_backend;
    sess->recv_from_backend     = modbustcp_recv_from_backend;
    sess->sess_type             = IOT_PROTO_TYPE_MODBUSTCP;
    sess->event_callback        = default_modbustcp_event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}

void frontend_engine_destory(){}