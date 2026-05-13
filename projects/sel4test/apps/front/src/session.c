
#include "session.h"
#include "frontend_proto.h"
#include "common_utils.h"

#include "frontend_api.h"

IoTFrontendSession *frontend_bluetooth_sess = NULL;
IoTFrontendSession *frontend_can_sess = NULL;
IoTFrontendSession *frontend_zigbee_sess = NULL;
IoTFrontendSession *frontend_lora_sess = NULL;
IoTFrontendSession *frontend_powerlink_sess = NULL;
IoTFrontendSession *frontend_modbustcp_sess = NULL;

/**
 * @brief Allocates and initializes a SessMsgSeg structure
 * 
 * @param len        Length of the data buffer (in bytes)
 * @param type   Memory source type of the data buffer (dynamic allocation or shared memory)
 * @param shared_data Pointer to shared memory data (valid only when data_src is SESS_MSG_SEG_SHARED_MEM)
 * @param mem_pool Pointer to the SharedMemoryPool instance (valid only when type is SESS_MSG_SEG_SHARED_MEM). 
                   Used to associate the SessMsgSeg with the shared memory pool for management (e.g., validation, release tracking).
 * 
 * @return Pointer to the newly allocated SessMsgSeg on success; NULL on failure
 * 
 * @note - If data_src is SESS_MSG_SEG_DYNAMIC_ALLOC: allocates data buffer with malloc()
 *       - If data_src is SESS_MSG_SEG_SHARED_MEM: uses shared_data directly (does not allocate new memory)
 *       - Initializes TAILQ entry to default state
 */
struct SessMsgSeg *sess_msg_seg_alloc(size_t len, SessMsgSegType type, uint8_t *shared_data, struct SharedMemoryPool *mem_pool){
    struct SessMsgSeg *msg_seg;
    msg_seg = malloc(sizeof(struct SessMsgSeg));

    if(NULL == msg_seg){
        error_print("sess_msg_seg_alloc failed: insurficient memory resource for message segment!");
        return NULL;
    }

    switch(type) {
        case SESS_MSG_SEG_DYNAMIC_ALLOC:
            msg_seg->data = malloc(len);

            if(NULL == msg_seg->data){
                error_print("sess_msg_seg_alloc failed: insurficient memory resource for message data!");
                goto msg_alloc_error;
            }
            break;
        case SESS_MSG_SEG_SHARED_MEM:
/*
 * The memory for storing data is prealloc from the memory pool.
 */
            if(NULL == mem_pool){
                error_print("sess_msg_seg_alloc failed: Shared memory pool (mem_pool) cannot be NULL when using SESS_MSG_SEG_SHARED_MEM type!");
                goto msg_alloc_error;
            }

            msg_seg->data       = shared_data;
            msg_seg->mem_pool   = mem_pool;
            break;

        default:
/*
 * Unsupported message segment type.
 */
            error_print("sess_msg_seg_alloc failed: unsupported message segment type!");
            goto msg_alloc_error;
    }

    msg_seg->type   = type;
    msg_seg->len    = len;
    msg_seg->offset = 0;

    return msg_seg;
msg_alloc_error:
    free(msg_seg);
    return NULL;
}



/**
 * @brief Lightweight allocation and initialization of a SessMsgSeg structure
 * 
 * A minimal version of message segment allocation that only initializes the core type field,
 * with no data buffer allocation or data copy operations, ensuring ultra-low overhead.
 * 
 * @param type Type of the message segment (must be a valid enumeration value < SESS_MSG_SEG_TYPE_MAX)
 * 
 * @return Pointer to the newly allocated and initialized SessMsgSeg on success; NULL on failure
 *         (e.g., invalid type, insufficient memory for the SessMsgSeg structure itself)
 * 
 * @note - Lightweight design: Does not allocate data buffers, not associate shared data,
 *         and only initializes the `type` field and default state of basic structure members.
 *       - Default initializations: The `len` field is set to 0, `shared_data` to NULL,
 *         reference count (`ref_cnt`) to 1, and linked list pointer (`next`) to NULL.
 *       - If data association is required (e.g., binding shared data), use supplementary interfaces
 *         or the full-version `sess_msg_seg_alloc()` function.
 *       - The structure's memory is allocated via the default lightweight memory mechanism
 *         (no external memory pool dependency, ensuring minimal allocation overhead).
 */
struct SessMsgSeg* sess_msg_seg_alloc_lite(SessMsgSegType type){
    struct SessMsgSeg *msg_seg;

    if(SESS_MSG_SEG_DYNAMIC_ALLOC != type || SESS_MSG_SEG_SHARED_MEM != type){
        error_print("sess_msg_seg_alloc failed: invalid! message segment type!\n");
        return NULL;
    }

    msg_seg = malloc(sizeof(struct SessMsgSeg));

    if(NULL == msg_seg){
        error_print("sess_msg_seg_alloc failed: insurficient memory resource for message segment!\n");
        return NULL;
    }

    msg_seg->type = type;

    return msg_seg;
}

/**
 * @brief Releases a SessMsgSeg structure and its associated resources
 * 
 * @param seg_ptr Double pointer to the SessMsgSeg to be released (will be set to NULL after release)
 * 
 * @note - If type is SESS_MSG_SEG_DYNAMIC_ALLOC: frees the data buffer with free()
 *       - If type is SESS_MSG_SEG_SHARED_MEM: does not free data (managed by shared memory system)
 *       - Safely handles NULL input (no operation performed)
 */
void sess_msg_seg_free(struct SessMsgSeg *seg_ptr){
    struct SessMsgSeg *msg_seg;

    if(NULL == seg_ptr){
        error_print("sess_msg_seg_free failed: input pointer is invalid!");
        return;
    }

    msg_seg = seg_ptr;


    switch(msg_seg->type) {
        case SESS_MSG_SEG_DYNAMIC_ALLOC:
            if(NULL == msg_seg->data){
                return;
            }
            free(msg_seg->data);
            break;
        case SESS_MSG_SEG_SHARED_MEM:
/*
 * The memory belongs to the shared memory pool.
 */
            break;

        default:
/*
 * Unsupported message segment type.
 */
            error_print("sess_msg_seg_free failed: unsupported message segment type!");
    }

    free(msg_seg);
}


/**
 * @brief Release all SessMsgSeg elements in the SessMsgQueue
 * 
 * This function traverses the SessMsgQueue, releases each SessMsgSeg element
 * according to its memory type, and finally clears the queue.
 * 
 * For dynamic allocation type (SESS_MSG_SEG_DYNAMIC_ALLOC):
 * - Free the data buffer allocated by malloc()
 * - Free the SessMsgSeg structure itself
 * 
 * For shared memory type (SESS_MSG_SEG_SHARED_MEM):
 * - Do not free the shared data buffer (managed by SharedMemoryPool)
 * - Only free the SessMsgSeg structure itself
 * 
 * @param queue Pointer to the SessMsgQueue to be cleared
 */
void sess_msg_queue_free_all(struct SessMsgQueue *queue) {
    if (queue == NULL) {
        return; // Avoid null pointer operation
    }

    struct SessMsgSeg *seg, *next_seg;


    TAILQ_FOREACH_SAFE(seg, queue, entry, next_seg) {
        /* 1. Remove the segment from the queue */
        TAILQ_REMOVE(queue, seg, entry);

        /* 2. Deallocate memory based on segment type */
        if (seg->type == SESS_MSG_SEG_DYNAMIC_ALLOC) {
            // Free dynamically allocated data buffer
            free(seg->data);
        } else if (seg->type == SESS_MSG_SEG_SHARED_MEM) {

            // if (current_seg->mem_pool) {
            //     shared_memory_pool_release(current_seg->mem_pool, current_seg->data);
            // }
        }
        /* 3. Free the segment structure itself */
        free(seg);

    }// TAILQ_FOREACH_SAFE


#if 0
    TAILQ_FOREACH(seg, queue, entry){
        // Manually save the next node before releasing current node
        next_seg = TAILQ_NEXT(seg, entry);

        // Remove current segment from the queue
        TAILQ_REMOVE(queue, seg, entry);

        // Free resources based on memory type
        if (SESS_MSG_SEG_DYNAMIC_ALLOC == seg->type) {
            // Free dynamically allocated data buffer
            if (NULL != seg->data) {
                free(seg->data);
                seg->data = NULL;
            }
        }

        if (SESS_MSG_SEG_SHARED_MEM == seg->type) {
            // Free dynamically allocated data buffer
            if (NULL != seg->data || NULL != seg->mem_pool) {
                free_shared_mem(seg->mem_pool, (uint64_t)seg->data);
            }
        }


        // Shared memory data is managed by SharedMemoryPool, no need to free here

        // Free the SessMsgSeg structure itself
        free(seg);

        // Move to next node (since current node is freed)
        seg = next_seg;
    }
#endif

    TAILQ_INIT(queue);
}


int session_send(struct FrontendSession* sess, const uint8_t* data, uint32_t size)
{
    return 0;
}


int session_recv(struct FrontendSession* sess, uint8_t* data, uint32_t size)
{
    return 0;
}



/**
 * @brief Send Bluetooth data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (Bluetooth protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains Bluetooth data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int bluetooth_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    GeneralProxyMsgHeader   iot_proxy_msg_hdr;
    uint8_t                 *payload, **res_pointer;
    uint8_t                 *res_buf[100] = {NULL};
    int                     ret;
    char dest[18] =         "A8:41:F4:8C:7F:E6";



    if(NULL == sess || NULL == msg_buf){
        error_print("bluetooth_send_to_backend failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("Bluetooth datalen = %d\n", msg_buf->len);

    iot_proxy_msg_hdr.outer_header.backend_sess_id      = 0;
    iot_proxy_msg_hdr.outer_header.frontend_sess_id     = 0;
    iot_proxy_msg_hdr.outer_header.proxy_msg_type       = PROXY_MSG_TYPE_IOT;
    iot_proxy_msg_hdr.outer_header.version              = PROXY_PROTO_VERSION_1;

    iot_proxy_msg_hdr.inner_header.iot_hdr.dev_port_id  = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.opcode       = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_type   = IOT_PROTO_TYPE_BLUETOOTH;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_ver    = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.payload_len  = msg_buf->len + get_iot_addr_length(IOT_PROTO_TYPE_BLUETOOTH);
    iot_proxy_msg_hdr.inner_header.iot_hdr.reserve      = 0;

    iot_proxy_msg_hdr.iot_addr_len                      = get_iot_addr_length(IOT_PROTO_TYPE_BLUETOOTH);
    iot_proxy_msg_hdr.iot_addr.addr_type                = IOT_PROTO_TYPE_BLUETOOTH;
    iot_proxy_msg_hdr.iot_addr.addr_info.bt_addr.port   = 1001;
    memcpy(iot_proxy_msg_hdr.iot_addr.addr_info.bt_addr.mac, dest, sizeof(iot_proxy_msg_hdr.iot_addr.addr_info.bt_addr.mac));


    res_pointer = res_buf;
    ret = build_proxy_general_message(sess->eng, &iot_proxy_msg_hdr, msg_buf->data, msg_buf->len, res_pointer, MEMORY_ALLOC_AMPQUEUE, NULL);

    return ret;
}

/**
 * @brief Receive Bluetooth data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (Bluetooth protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int bluetooth_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    utils_print("In %s\n", __func__);
    sess->event_callback(sess, IOT_SESS_EVENT_DATA_RECEIVED, msg_buf);

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Send CAN data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (CAN protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains CAN data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int can_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    GeneralProxyMsgHeader   iot_proxy_msg_hdr;
    uint8_t                 *payload, **res_pointer;
    uint8_t                 *res_buf[100] = {NULL};
    int                     ret;

    if(NULL == sess || NULL == msg_buf){
        error_print("can_send_to_backend failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("CAN datalen = %d\n", msg_buf->len);

    iot_proxy_msg_hdr.outer_header.backend_sess_id          = 0;
    iot_proxy_msg_hdr.outer_header.frontend_sess_id         = 0;
    iot_proxy_msg_hdr.outer_header.proxy_msg_type           = PROXY_MSG_TYPE_IOT;
    iot_proxy_msg_hdr.outer_header.version                  = PROXY_PROTO_VERSION_1;

    iot_proxy_msg_hdr.inner_header.iot_hdr.dev_port_id      = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.opcode           = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_type       = IOT_PROTO_TYPE_CAN;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_ver        = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.payload_len      = msg_buf->len + get_iot_addr_length(IOT_PROTO_TYPE_CAN);
    iot_proxy_msg_hdr.inner_header.iot_hdr.reserve          = 0;

    iot_proxy_msg_hdr.iot_addr_len                          = get_iot_addr_length(IOT_PROTO_TYPE_CAN);
    iot_proxy_msg_hdr.iot_addr.addr_type                    = IOT_PROTO_TYPE_CAN;
    iot_proxy_msg_hdr.iot_addr.addr_info.can_addr.bus_id    = 0;
    iot_proxy_msg_hdr.iot_addr.addr_info.can_addr.can_id    = 0;
    iot_proxy_msg_hdr.iot_addr.addr_info.can_addr.port      = 0;

    res_pointer = res_buf;
    ret = build_proxy_general_message(sess->eng, &iot_proxy_msg_hdr, msg_buf->data, msg_buf->len, res_pointer, MEMORY_ALLOC_AMPQUEUE, NULL);

    return ret;
}


/**
 * @brief Receive CAN data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (CAN protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int can_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    utils_print("In %s\n", __func__);

    sess->event_callback(sess, IOT_SESS_EVENT_DATA_RECEIVED, msg_buf);

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Send Zigbee data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (Zigbee protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains Zigbee data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int zigbee_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Receive Zigbee data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (Zigbee protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int zigbee_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Send LoRa data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (LoRa protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains LoRa data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int lora_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Receive LoRa data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (LoRa protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int lora_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Send PowerLink data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (PowerLink protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains PowerLink data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int powerlink_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Receive PowerLink data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (PowerLink protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int powerlink_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Send ModbusTCP data to backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (ModbusTCP protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (contains ModbusTCP data + destination address)
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int modbustcp_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    GeneralProxyMsgHeader   iot_proxy_msg_hdr;
    uint8_t                 *payload, **res_pointer;
    uint8_t                 *res_buf[100] = {NULL};
    IPv4PortTuple           ipv4_port_tuple;
    char                    ip_port_str[] = "192.168.137.2:502";
    int                     ret;
    
    if(NULL == sess || NULL == msg_buf){
        error_print("modbustcp_send_to_backend failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    IPV4_PORT_STR_TO_TUPLE(ip_port_str, ipv4_port_tuple);

    utils_print("ModbusTcp datalen = %d\n", msg_buf->len);

    iot_proxy_msg_hdr.outer_header.backend_sess_id                  = 0;
    iot_proxy_msg_hdr.outer_header.frontend_sess_id                 = 0;
    iot_proxy_msg_hdr.outer_header.proxy_msg_type                   = PROXY_MSG_TYPE_IOT;
    iot_proxy_msg_hdr.outer_header.version                          = PROXY_PROTO_VERSION_1;
    res_pointer                                                     = res_buf;

    iot_proxy_msg_hdr.inner_header.iot_hdr.dev_port_id              = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.opcode                   = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_type               = IOT_PROTO_TYPE_MODBUSTCP;
    iot_proxy_msg_hdr.inner_header.iot_hdr.proto_ver                = 0;
    iot_proxy_msg_hdr.inner_header.iot_hdr.payload_len              = msg_buf->len + get_iot_addr_length(IOT_PROTO_TYPE_MODBUSTCP);
    iot_proxy_msg_hdr.inner_header.iot_hdr.reserve                  = 0;

    iot_proxy_msg_hdr.iot_addr_len                                  = get_iot_addr_length(IOT_PROTO_TYPE_MODBUSTCP);
    iot_proxy_msg_hdr.iot_addr.addr_type                            = IOT_PROTO_TYPE_MODBUSTCP;
    iot_proxy_msg_hdr.iot_addr.addr_info.modbus_tcp_addr.reg_addr   = 0;
    iot_proxy_msg_hdr.iot_addr.addr_info.modbus_tcp_addr.reg_num    = 1;
    iot_proxy_msg_hdr.iot_addr.addr_info.modbus_tcp_addr.unit_id    = 0;
    iot_proxy_msg_hdr.iot_addr.addr_info.modbus_tcp_addr.port       = ipv4_port_tuple.port;
    memcpy(iot_proxy_msg_hdr.iot_addr.addr_info.modbus_tcp_addr.ip, ipv4_port_tuple.ipv4_addr.data, sizeof(ipv4_port_tuple.ipv4_addr.data));

    res_pointer = res_buf;
    ret = build_proxy_general_message(sess->eng, &iot_proxy_msg_hdr, msg_buf->data, msg_buf->len, res_pointer, MEMORY_ALLOC_AMPQUEUE, NULL);

    return ret;
}


/**
 * @brief Receive ModbusTCP data from backend via frontend session
 * @param sess Pointer to IoTFrontendSession instance (ModbusTCP protocol session)
 * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
 * @param data Raw data buffer received from backend
 * @param data_len Length of raw data received from backend
 * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
 */
int modbustcp_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len){
    utils_print("%s\n");
    sess->event_callback(sess, IOT_SESS_EVENT_DATA_RECEIVED, msg_buf);
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Default Bluetooth event callback for frontend session
 *
 * Default handler for Bluetooth frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_bluetooth_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);

    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        utils_print("IOT_SESS_EVENT_DATA_RECEIVED happens!\n");
        utils_print("bluetooth message length is %d ,content is %s\n", msg_buf->len, msg_buf->data);
        frontend_iot_sess_send(sess, "test bluetooth", strlen("test bluetooth"));
    }
}

/**
 * @brief Default CAN event callback for frontend session
 *
 * Default handler for CAN frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_can_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        utils_print("IOT_SESS_EVENT_DATA_RECEIVED happens!\n");
        frontend_iot_sess_send(sess, "Hello", strlen("Hello"));
    }
}

/**
 * @brief Default Zigbee event callback for frontend session
 *
 * Default handler for Zigbee frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_zigbee_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        
    }
}

/**
 * @brief Default LoRa event callback for frontend session
 *
 * Default handler for LoRa frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_lora_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        
    }
}

/**
 * @brief Default PowerLink event callback for frontend session
 *
 * Default handler for PowerLink frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_powerlink_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        
    }
}

/**
 * @brief Default ModbusTCP event callback for frontend session
 *
 * Default handler for ModbusTCP frontend session events:
 * - Prints callback entry log
 * - Processes IOT_SESS_EVENT_DATA_RECEIVED event (reserved for data handling)
 *
 * @param sess Pointer to IoTFrontendSession instance
 * @param event IoT session event type (e.g., data received/link state changed)
 * @param msg_buf Pointer to IotMsgBuffer (event-related data buffer)
 * @return None
 */
void default_modbustcp_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf){
    utils_print("In %s\n", __func__);
    if(IOT_SESS_EVENT_DATA_RECEIVED == event){
        
    }
}


