#ifndef ENGINE_H
#define ENGINE_H

// #include <arpa/inet.h>
// #include <netinet/in.h>

#include "dev.h"
#include "session_pool.h"
#include "shared_mem_io.h"
#include "common_utils.h"


#define HS_NET_DEV_CFG "hs_net_dev.ini"

extern volatile HyperampShmQueue *g_hyper_tx_queue;  // seL4 → Linux (seL4 writes requests, Linux reads)
extern volatile HyperampShmQueue *g_hyper_rx_queue;  // Linux → seL4 (seL4 reads responses, Linux writes)
extern volatile void *g_hyper_data_region;           // Shared data buffer referenced by entries in TX/RX queues
/*
 * Check if the string is a valid IPv4 address
 * Parameter: ip_str - string to check
 * Returns: 1 - valid IPv4, 0 - invalid
 */
#define DEV_IS_IPV4(ip_str) ({ \
    struct in_addr addr; \
    inet_pton(AF_INET, (ip_str), &addr) == 1; \
})

/*
 * Check if the string is a valid IPv6 address
 * Parameter: ip_str - string to check
 * Returns: 1 - valid IPv4, 0 - invalid
 */
#define DEV_IS_IPV6(ip_str) ({ \
    struct in6_addr addr; \
    inet_pton(AF_INET6, (ip_str), &addr) == 1; \
})

/*
 * Check if the string is a valid IP address (IPv4 or IPv6)
 * Parameter: ip_str - string to check
 * Returns: 1 - valid IP, 0 - invalid
 */
#define DEV_IS_VALID_IP(ip_str) (DEV_IS_IPV4(ip_str) || DEV_IS_IPV6(ip_str))

/*
 * Get the IP address type
 * Parameter: ip_str - string to check
 * Returns: SESS_NON_IP_PROTO - invalid, SESS_IPV4_PROTO - IPv4, SESS_IPV6_PROTO - IPv6
 */
#define DEV_IP_TYPE(ip_str) ({ \
    int type = SESS_NON_IP_PROTO; \
    if (DEV_IS_IPV4(ip_str)) { \
        type = SESS_IPV4_PROTO; \
    } else if (DEV_IS_IPV6(ip_str)) { \
        type = SESS_IPV6_PROTO; \
    } \
    type; \
})


#define DEV_IS_VALID_IP(ip_str) (DEV_IS_IPV4(ip_str) || DEV_IS_IPV6(ip_str))

struct FrontendEngOps; 

/**
 * @brief Frontend engine command state enumeration
 *
 * Enumerates all possible states of device (dev) and strategy (strgy) message transmission 
 * between frontend engine and backend.
 */
typedef enum {
    FRONTEND_CMD_READY,    ///< Ready for transmission (no pending dev/strgy messages)
    FRONTEND_CMD_WAITTING, ///< Dev/strgy message is being constructed, waiting for backend processing
    FRONTEND_CMD_OK,       ///< Dev/strgy message processed successfully by backend
    FRONTEND_CMD_ERROR     ///< Dev/strgy message processing failed by backend
} FrontendCmdState;

typedef struct FrontendEngine_{
    uint16_t                            dev_num;
    FrontendHighSpeedNetDeviceSet       *dev_set;      // High speed network device set
    FrontendIoTDeviceSet                *iot_dev_set;  // IoT device set
    uint16_t                            iot_dev_num;    // IoT device number
    uint16_t                            sel_id;
    uint16_t                            active_mask;    // Show the positions of all the active high-speed network devices as a mask.
    uint16_t                            eng_cmd_id;     // Used when constructing device (dev) messages and strategy (strgy) messages, filled into the msg_id member of these messages.
/*
 * Maintains the state of dev message and strgy message transmitted by the engine. Supported states: FRONTEND_CMD_READY (transmittable), FRONTEND_CMD_WAITTING (dev/strgy message 
 * is being constructed, waiting for backend processing), FRONTEND_CMD_OK (dev/strgy message processed successfully by backend), FRONTEND_CMD_ERROR (dev/strgy message processing 
 * failed by backend). The state will be restored to FRONTEND_CMD_READY after proper handling of FRONTEND_CMD_OK and FRONTEND_CMD_ERROR.
 */
    FrontendCmdState                    eng_cmd_state; 
    struct FrontendSessionPool          *sess_pool;         // Session pool
    struct SharedMemoryPool             *mem_pool;          // Shared memory pool
    struct SharedMemoryPoolLock         *mem_pool_lock;     // Shared memory pool lock
    struct SharedMemoryPoolQueue        *rx_queue;          // RX queue
    struct SharedMemoryPoolQueue        *tx_queue;          // TX queue
    volatile HyperampShmQueue           *hyper_rx_queue;    // Hyper RX queue
    volatile HyperampShmQueue           *hyper_tx_queue;
/*
 * Base address of the shared memory region backing HyperAMP queues, mapped into seL4 Linux address space for cross-environment communication. 
 */
    volatile void                       *hyper_amp_data_region; 
    struct FrontendEngOps               *ops;
} FrontendEngine;


struct FrontendEngOps {
    int (*enable_dev)(FrontendEngine *eng, uint16_t mask);                            // Enable devices in the high-speed network device set
    int (*disable_dev)(FrontendEngine *eng, uint16_t mask);                           // Disable devices in the high-speed network device set
    int (*query_dev)(FrontendEngine *eng, uint16_t *mask);                            // Query information/status of devices in the high-speed network device set
    // int (*choose_dev)(FrontendEngine *eng, uint16_t *dev_id);                         // Choose the most appropriate high-speed network device over which to establish a session, and record its device ID
    int (*conf_dev_selector)(FrontendEngine *eng, uint16_t sel_id);                   // Configure the device selection function/strategy for the high-speed network device set
    int (*query_dev_sel_id)(FrontendEngine *eng, uint16_t *sel_id);                   // Query the ID of the current device selector in the backend engine
};

#define HS_DEV_SELECTOR_NAME_LEN                                       30
#define HS_DEV_SELECTOR_NUM                                            3


typedef struct{
    char sel_name[HS_DEV_SELECTOR_NAME_LEN];
    int (*choose_dev)(FrontendEngine *eng, uint16_t *dev_id);
} HSDevSelector;


extern FrontendEngine *p_g_fr_eng;


void frontend_engine_init();
void frontend_engine_run();
void frontend_engine_run_hyperamp();
void frontend_engine_destory();

FrontendEngine *frontend_get_global_engine();

int frontend_engine_init_eng_ops(FrontendEngine *eng);
int engine_init_selector(FrontendEngine *eng);


int engine_init_hs_net_dev(FrontendEngine *eng);
int engine_init_sess_pool(FrontendEngine *eng);
int engine_init_shared_mem_pool(FrontendEngine *eng);
int engine_init_shared_mem_pool_lock(FrontendEngine *eng);
int engine_init_shared_mem_queue(FrontendEngine *eng);


void engine_destory_hs_net_dev(FrontendEngine *eng);
void engine_destory_sess_pool(FrontendEngine *eng);
void engine_destory_mem_pool(FrontendEngine *eng);
void engine_destory_mem_pool_lock(FrontendEngine *eng);

int engine_choose_hs_net(FrontendEngine *eng, int *selected_dev_id);


int engine_init_hyperamp_queue(FrontendEngine *eng);


/**
 * @brief Get the f2b queue from FrontendEngine's session pool
 * 
 * This macro retrieves the f2b queue from the FrontendSessionPool associated with a FrontendEngine.
 * It includes null pointer checks for the engine and its session pool. Error messages are printed
 * via error_print() when checks fail, with the macro name included for debugging.
 * 
 * @param engine Pointer to a FrontendEngine structure; must not be NULL for valid queue retrieval
 * @param result_var Variable to store the result (pointer to FrontendSessionQueue or NULL)
 * 
 * @note The result is stored in the provided result_var, which should be of type
 *       'struct FrontendSessionQueue *'
 */
#define FRONTEND_ENGINE_GET_F2B_QUEUE(engine, result_var) \
    do { \
        /* Initialize result to NULL by default */ \
        (result_var) = NULL; \
 \
        /* Check if FrontendEngine pointer is NULL */ \
        utils_print("In FRONTEND_ENGINE_GET_F2B_QUEUE, address of engine is %p \n", engine); \
        if (!(engine)) { \
            error_print("[FRONTEND_ENGINE_GET_F2B_QUEUE] Error:FrontendEngine pointer is NULL when getting f2b queue"); \
        } \
        /* Check if session pool within frontEngine is NULL */ \
        else if (!(engine)->sess_pool) { \
            error_print("[FRONTEND_ENGINE_GET_F2B_QUEUE] Error: FrontendSessionPool in frontEngine is NULL when getting f2b queue"); \
        } \
        /* All checks passed - retrieve the f2b queue */ \
        else { \
            (result_var) = &(engine)->sess_pool->queue_f2b; \
        } \
    } while (0)

/**
 * @brief Get the b2f queue from FrontendEngine's session pool
 * 
 * This macro retrieves the b2f queue from the FrontendSessionPool associated with a FrontendEngine.
 * It includes null pointer checks for the engine and its session pool. Error messages are printed
 * via error_print() when checks fail, with the macro name included for debugging.
 * 
 * @param engine Pointer to a FrontendEngine structure; must not be NULL for valid queue retrieval
 * @param result_var Variable to store the result (pointer to FrontendSessionQueue or NULL)
 * 
 * @note The result is stored in the provided result_var, which should be of type
 *       'struct FrontendSessionQueue *'
 */
#define FRONTEND_ENGINE_GET_B2F_QUEUE(engine, result_var) \
    do { \
        /* Initialize result to NULL by default */ \
        (result_var) = NULL; \
 \
        /* Check if FrontendEngine pointer is NULL */ \
        if (!(engine)) { \
            error_print("[FRONTEND_ENGINE_GET_B2F_QUEUE] Error: FrontendEngine pointer is NULL when getting b2f queue"); \
        } \
        /* Check if session pool within FrontendEngine is NULL */ \
        else if (!(engine)->sess_pool) { \
            error_print("[FRONTEND_ENGINE_GET_B2F_QUEUE] Error: FrontendSessionPool in FrontendEngine is NULL when getting b2f queue"); \
        } \
        /* All checks passed - retrieve the b2f queue */ \
        else { \
            (result_var) = &(engine)->sess_pool->queue_b2f; \
        } \
    } while (0)


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
                               size_t buf_max_len, size_t *out_len);

/**
 * @brief Send data through the specified TX queue (residing in shared memory)
 * @param queue Pointer to the SharedMemoryPoolQueue (TX queue) to operate on
 * @param[in] data_ptr Double pointer to the data in shared memory to be sent
 *                     (points to actual data location in shared memory)
 * @param data_len Length of the data to be sent (in bytes)
 * @param[out] sent_len Pointer to store the actual length of data sent (in bytes)
 * @return FRONTEND_PROXY_PROCESS_OK if data is sent successfully;
 *         FRONTEND_PROXY_PROCESS_ERROR if a system-level error occurs (e.g., queue access violation);
 *         FRONTEND_PROXY_PROCESS_AGAIN if data cannot be sent temporarily (e.g., queue is full)
 * @note The caller is responsible for managing the lock of the shared memory pool
 *       (lock once before multiple calls to reduce overhead)
 */
int frontend_engine_tx_queue_send(struct SharedMemoryPoolQueue *queue, const void **data_ptr, 
                                size_t data_len, size_t *sent_len);



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
                                          int *ret, size_t *out_len);


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
                                          uint8_t *data, size_t *out_len);


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
                                          const uint8_t *data);

struct FrontendEngOps *get_hs_frontend_engine_ops();


void frontend_engine_init();

int engine_init_iot_devices(FrontendEngine *eng);
int engine_init_iot_sessions(FrontendEngine *engine);

int engine_init_bluetooth_session(IotDevice *dev, IoTFrontendSession *sess);

int engine_init_can_session(IotDevice *dev, IoTFrontendSession *sess);

int engine_init_zigbee_session(IotDevice *dev, IoTFrontendSession *sess);

int engine_init_lora_session(IotDevice *dev, IoTFrontendSession *sess);

int engine_init_powerlink_session(IotDevice *dev, IoTFrontendSession *sess);

int engine_init_modbustcp_session(IotDevice *dev, IoTFrontendSession *sess);

#endif