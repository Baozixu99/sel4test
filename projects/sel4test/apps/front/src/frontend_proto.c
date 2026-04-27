#include "engine.h"
#include "frontend_proto.h"


uint8_t global_amp_tx_buf[HYPERAMP_MSG_HDR_PLUS_MAX_SIZE];

/*
 *  Functions for building sub-type proxy messages.
 *
 * - build_proxy_dev_message   : Builds a device-specific proxy message
 * - build_proxy_strgy_message : Builds a strategy-specific proxy message
 * - build_proxy_sess_message  : Builds a session-specific proxy message
 * - build_proxy_data_message  : Builds a data-specific proxy message
 */


/**
 * @brief Builds a complete proxy device message by combining the device header and payload.
 * Builds a complete proxy device message by combining the device message header and payload.
 * The function will allocate memory for the output message (caller is responsible for freeing it).
 * @param[in] dev_hdr Pointer to a DevMsgHeader structure specifying the device message header.
 * Must not be NULL.
 * @param[in] payload Pointer to the const uint8_t buffer containing the device message payload.
 * Can be NULL only if payload_len is 0.
 * @param[in] payload_len Length of the payload in bytes. Must be non-negative and match
 * dev_hdr->payload_len (if header contains payload length field) for consistency.
 * @param[out] result_msg Double pointer to receive the address of the constructed proxy device message.
 * On success, points to a newly allocated buffer containing the complete device message.
 * Caller must free this memory with appropriate function when done.
 * Must not be NULL.
 * @return int Returns FRONTEND_PROXY_PROCESS_OK on successful message construction;
 * Returns FRONTEND_PROXY_PROCESS_ERROR if any parameter is invalid (e.g., NULL pointers,
 * mismatched lengths) or memory allocation fails.
 */
 int build_proxy_dev_message(DevMsgHeader *dev_hdr, const uint8_t *payload, size_t payload_len, uint8_t **result_msg){
    DevMsgHeader *header;
    size_t corr_len;
    uint8_t *dev_msg;

    
    corr_len = DEV_MSG_HEADER_PAYLOAD_LEN(dev_hdr);
    utils_print("corr_len = %d, payload_len = %d\n", corr_len, payload_len);

    if(payload_len != corr_len){
        error_print("build_proxy_dev_message failed: payload length does not match expected value based on message type and action type!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(payload_len != dev_hdr->payload_len){
        error_print("build_proxy_dev_message failed: payload length does not match the payload_len field in DevMsgHeader!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    dev_msg                 = *result_msg;
    header                  = (DevMsgHeader *)dev_msg;
    header->version         = dev_hdr->version;
    header->msg_type        = dev_hdr->msg_type;
    header->msg_id          = dev_hdr->msg_id;
    header->action_type     = dev_hdr->action_type;
    header->payload_len     = payload_len;

    dev_msg += sizeof(DevMsgHeader);

    utils_print("In %s, before memcpy\n", __func__);
    memcpy(dev_msg, payload, payload_len);

    return FRONTEND_PROXY_PROCESS_OK;
 }


/**
 * @brief Builds a complete proxy strategy message by combining the strategy header and payload.
 * Builds a complete proxy strategy message by combining the strategy message header and payload.
 * The function will allocate memory for the output message (caller is responsible for freeing it).
 * @param[in] strgy_hdr Pointer to a StrgyMsgHeader structure specifying the strategy message header.
 * Must not be NULL.
 * @param[in] payload Pointer to the const uint8_t buffer containing the strategy message payload.
 * Can be NULL only if payload_len is 0.
 * @param[in] payload_len Length of the payload in bytes. Must be non-negative and match
 * strgy_hdr->payload_len (if header contains payload length field) for consistency.
 * @param[out] result_msg Double pointer to receive the address of the constructed proxy strategy message.
 * On success, points to a newly allocated buffer containing the complete strategy message.
 * Caller must free this memory with appropriate function when done.
 * Must not be NULL.
 * @return int Returns FRONTEND_PROXY_PROCESS_OK on successful message construction;
 * Returns FRONTEND_PROXY_PROCESS_ERROR if any parameter is invalid (e.g., NULL pointers,
 * mismatched lengths) or memory allocation fails.
 */
int build_proxy_strgy_message(StrgyMsgHeader *strgy_hdr, const uint8_t *payload, size_t payload_len, uint8_t **result_msg){
    StrgyMsgHeader *header;
    size_t corr_len;
    uint8_t *strgy_msg;

    corr_len = STRGY_MSG_HEADER_PAYLOAD_LEN(strgy_hdr);

    if(payload_len != corr_len){
        error_print("build_proxy_strgy_message failed: payload length does not match expected value based on message type and action type!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(payload_len != strgy_hdr->payload_len){
        error_print("build_proxy_strgy_message failed: payload length does not match the payload_len field in StrgyMsgHeader!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    strgy_msg               = *result_msg;
    header                  = (StrgyMsgHeader *)strgy_msg;
    header->version         = strgy_hdr->version;
    header->msg_type        = strgy_hdr->msg_type;
    header->msg_id          = strgy_hdr->msg_id;
    header->action_type     = strgy_hdr->action_type;
    header->payload_len     = payload_len;

    strgy_msg += sizeof(StrgyMsgHeader);
    memcpy(strgy_msg, payload, payload_len);

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Builds a complete proxy session message by combining the session header and payload.
 * Builds a complete proxy session message by combining the session message header and payload.
 * The function will allocate memory for the output message (caller is responsible for freeing it).
 * @param[in] sess_hdr Pointer to a SessMsgHeader structure specifying the session message header.
 * Must not be NULL.
 * @param[in] payload Pointer to the const uint8_t buffer containing the session message payload.
 * Can be NULL only if payload_len is 0.
 * @param[in] payload_len Length of the payload in bytes. Must be non-negative and match
 * sess_hdr->payload_len (if header contains payload length field) for consistency.
 * @param[out] result_msg Double pointer to receive the address of the constructed proxy session message.
 * On success, points to a newly allocated buffer containing the complete session message.
 * Caller must free this memory with appropriate function when done.
 * Must not be NULL.
 * @return int Returns FRONTEND_PROXY_PROCESS_OK on successful message construction;
 * Returns FRONTEND_PROXY_PROCESS_ERROR if any parameter is invalid (e.g., NULL pointers,
 * mismatched lengths) or memory allocation fails.
 */
int build_proxy_sess_message(SessMsgHeader *sess_hdr, const uint8_t *payload, size_t payload_len, uint8_t **result_msg){
    utils_print("In %s\n", __func__);
    SessMsgHeader *header;
    size_t corr_len;
    uint8_t *sess_msg;

    corr_len = SESS_MSG_HEADER_PAYLOAD_LEN(sess_hdr);

    utils_print("In func %s, corr_len = %d, payload_len = %d\n", __func__, corr_len, payload_len);

    if(payload_len != corr_len){
        error_print("build_proxy_sess_message failed: payload length does not match expected value based on message type, action type and IP version!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(payload_len != sess_hdr->payload_len){
        error_print("build_proxy_sess_message failed: payload length does not match the payload_len field in SessMsgHeader!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess_msg            = *result_msg;
    header              = (SessMsgHeader *)sess_msg;
    header->version     = sess_hdr->version;
    header->msg_type    = sess_hdr->msg_type;
    header->action_type = sess_hdr->action_type;
    header->ip_version  = sess_hdr->ip_version;
    header->payload_len = sess_hdr->payload_len;

    utils_print("In %s, version = %d, msg_type = %d, action_type = %d, ip_version = %d, payload_len = %d, address = %p\n", 
                __func__, header->version, header->msg_type, header->action_type, header->ip_version, header->payload_len, &header);
    
    sess_msg += sizeof(SessMsgHeader);

    if(0 == payload_len){
        if(NULL == payload){
            return FRONTEND_PROXY_PROCESS_OK;
        }else{
            error_print("build_proxy_sess_message failed: payload_len is 0, but payload is not NULL!\n\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
    }

    memcpy(sess_msg, payload, payload_len);

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Builds a complete proxy data message by combining the proxy message header and payload.
 * Builds a complete proxy data message by combining the proxy message header and payload.
 * The function will allocate memory for the output message (caller is responsible for freeing it).
 * @param[in] proxy_msg_hdr Pointer to a ProxyMsgHeader structure specifying the proxy data message header.
 * Must not be NULL.
 * @param[in] payload Pointer to the const uint8_t buffer containing the data message payload.
 * Can be NULL only if payload_len is 0.
 * @param[in] payload_len Length of the payload in bytes. Must be non-negative and match
 * proxy_msg_hdr->payload_len (if header contains payload length field) for consistency.
 * @param[out] result_msg Double pointer to receive the address of the constructed proxy data message.
 * On success, points to a newly allocated buffer containing the complete data message.
 * Caller must free this memory with appropriate function (e.g., free()) when done.
 * Must not be NULL.
 * @return int Returns BACKEND_PROXY_PROCESS_OK on successful message construction;
 * Returns BACKEND_PROXY_PROCESS_ERROR if any parameter is invalid (e.g., NULL pointers,
 * mismatched lengths) or memory allocation fails.
*/
int build_proxy_data_message(ProxyMsgHeader *proxy_msg_hdr, const uint8_t *payload, size_t payload_len, uint8_t **result_msg){
    printf("In %s\n", __func__);
    uint8_t *data_msg;
    printf("In %s, payload_len = %d, proxy_msg_hdr->payload_len = %d\n", __func__, payload_len, proxy_msg_hdr->payload_len);
    printf("frontend_sess_id = %d, frontend_sess_id =%d\n", proxy_msg_hdr->frontend_sess_id, proxy_msg_hdr->backend_sess_id);

#if 0
    if(payload_len != proxy_msg_hdr->payload_len){
        error_print("build_proxy_data_message failed: payload length does not match expected value based on message type, action type and IP version!");
        return BACKEND_PROXY_PROCESS_ERROR;
    }
#endif

    utils_print("Address of proxy data header = %p, content  =%p, size of ProxyMsgHeader = %d\n", proxy_msg_hdr, *result_msg, sizeof(ProxyMsgHeader));

    data_msg = *result_msg;
    memcpy(data_msg, payload, payload_len);

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Build IoT proxy message (supports 3-layer structure: ProxyMsgHeader + IotMsgHeader + IotAddr + payload)
 * 
 * Core construction function for IoT proxy messages (called by build_proxy_general_message):
 * 1. Validates IotMsgHeader (proto_type/opcode/payload_len) and IotAddr (addr_type matches proto_type)
 * 2. Calculates total length: sizeof(ProxyMsgHeader) + sizeof(IotMsgHeader) + header->iot_addr_len + payload_len
 * 3. Allocates memory for the full message (heap/pool based on caller context)
 * 4. Writes data in order:
 *    - ProxyMsgHeader (from header->outer_header)
 *    - IotMsgHeader (from header->inner_header.iot_hdr)
 *    - IotAddr (protocol-specific address from header->iot_addr, truncated to header->iot_addr_len)
 *    - Payload (raw data from payload parameter)
 * 5. Updates ProxyMsgHeader.total_len with the full message length
 * 
 * @param header Pointer to GeneralProxyMsgHeader (must contain valid iot_hdr + iot_addr + iot_addr_len)
 * @param payload Pointer to IoT payload (raw data after address; NULL if no payload)
 * @param payload_len Length of IoT payload (bytes; 0 if no payload)
 * @param result_msg Output pointer to constructed IoT proxy message (allocated internally)
 * @return int Construction result
 *         - FRONTEND_PROXY_PROCESS_OK: IoT message built successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid parameters/address/protocol mismatch/memory error)
 * 
 * @note Validates protocol consistency: header->inner_header.iot_hdr.proto_type must match header->iot_addr.addr_type
 * @note header->iot_addr_len must be >0 and ≤ sizeof(IotAddr) (e.g., 8 for bt_addr, 7 for can_addr)
 * @note IotMsgHeader.payload_len is automatically set to (header->iot_addr_len + payload_len)
 * @note Handles frontend-to-backend IoT messages transmitted via HyperAMP (same as other proxy messages)
 */
int build_proxy_iot_message(GeneralProxyMsgHeader *header, 
                            const uint8_t *payload, 
                            size_t payload_len, 
                            uint8_t **result_msg){
    utils_print("In %s\n", __func__);
    IotMsgHeader *iot_msg_hdr;
    uint8_t *iot_data_msg;
    size_t addr_info_len;

    /*
     * 1. Basic Parameter Validation (Safety Check)
     */
    if (!header || !payload || !result_msg || !(*result_msg)) {
        error_print("build_proxy_iot_message failed: NULL pointer argument!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Extract the IoT protocol type from the inner header to determine address size
    uint16_t proto_type = header->inner_header.iot_hdr.proto_type;

    utils_print("proto_type = %d\n", proto_type);
    utils_print("header->inner_header.iot_hdr.proto_type = %d, address of header->inner_header.iot_hdr.proto_type = %p\n",
                header->inner_header.iot_hdr.proto_type, &header->inner_header.iot_hdr.proto_type);
    
    // Optional Consistency Check: Ensure the header type matches the address type stored in IotAddr
    if (proto_type != header->iot_addr.addr_type) {
        error_print("build_proxy_iot_message warning: Header proto_type mismatch with IotAddr type!\n");
        // Proceeding anyway as the switch statement below will handle the actual size based on proto_type
    }

//    print_general_proxy_msg_header(header);
/*
 * Fill IoT header.
 */

    iot_data_msg                = *result_msg;
    iot_msg_hdr                 = (IotMsgHeader *)iot_data_msg;
    iot_msg_hdr->dev_port_id    = 0;
    iot_msg_hdr->opcode         = 0;
    iot_msg_hdr->proto_type     = header->inner_header.iot_hdr.proto_type;
    iot_msg_hdr->proto_ver      = 0;
    iot_msg_hdr->reserve        = 0;


    iot_data_msg = iot_data_msg + sizeof(IotMsgHeader);
    addr_info_len    = 0;

    /*
     * 2. Determine the exact valid length of the specific address structure
     * Purpose: Prevent copying uninitialized garbage data from the union.
     * Since we are NOT sending the 'addr_type' byte, we only copy the 'addr_info' union part.
     */

    switch (proto_type) {
        case IOT_PROTO_TYPE_BLUETOOTH:
            addr_info_len = sizeof(IotBtAddr);
            break;
        case IOT_PROTO_TYPE_CAN:
            addr_info_len = sizeof(IotCanAddr);
            break;
        case IOT_PROTO_TYPE_ZIGBEE:
            addr_info_len = sizeof(IotZigbeeAddr);
            break;
        case IOT_PROTO_TYPE_LORA:
            addr_info_len = sizeof(IotLoraAddr);
            break;
        case IOT_PROTO_TYPE_POWERLINK:
            addr_info_len = sizeof(IotPowerLinkAddr);
            break;
        case IOT_PROTO_TYPE_MODBUSTCP:
            addr_info_len = sizeof(IotModbusTcpAddr);
            break;
        default:
            error_print("build_proxy_iot_message failed: unsupported IoT protocol type!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // Sanity check: Ensure calculated length does not exceed the union's maximum size

    utils_print("addr_info_len = %d, sizeof union = %d, header->iot_addr_len = %d\n", addr_info_len, sizeof(header->iot_addr.addr_info), header->iot_addr_len);
    if (addr_info_len > sizeof(header->iot_addr.addr_info)) {
        error_print("build_proxy_iot_message failed: calculated address length exceeds union size!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
    
    /*
     * Refine length using 'iot_addr_len' if provided and smaller.
     * This handles cases where the valid address data is shorter than the struct size (e.g., padding).
     * Assumption: header->iot_addr_len refers only to the valid bytes within 'addr_info'.
     */
    if (header->iot_addr_len > 0 && header->iot_addr_len < addr_info_len) {
        addr_info_len = header->iot_addr_len;
    }

    /*
     * 3. Serialization Step A: Write Address Info Only
     * We copy ONLY the 'addr_info' union part. The 'addr_type' is excluded from the transmission buffer.
     */
    memcpy(iot_data_msg, &header->iot_addr.addr_info, addr_info_len);
    iot_data_msg += addr_info_len;

    /*
     * 4. Serialization Step B: Write Payload
     */
    if (payload_len > 0) {
        memcpy(iot_data_msg, payload, payload_len);
    }

    iot_msg_hdr->payload_len = payload_len + addr_info_len;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Builds a complete message by combining the general header and payload.
 * 
 * Builds a complete proxy general message by combining the general header and payload.
 * The function will allocate memory for the output message (caller is responsible for freeing it).
 * 
 * Constructs a complete proxy general message by integrating the provided header and payload.
 * The memory for the output message is managed based on the specified allocation mode:
 * For MEMORY_ALLOC_SHARED: Memory is allocated within shared memory, which is organized in a FIFO RING buffer.
 * The caller does not need to handle memory deallocation, as the shared memory is managed by the FIFO RING buffer mechanism.
 * For MEMORY_ALLOC_CALLER: Memory must be pre-allocated by the caller. The function will directly populate the
 * provided buffer without checking its size; the caller is solely responsible for ensuring the buffer is large
 * enough to hold the complete message (header + payload).
 * 
 * 
 * @param[in]  engine            Pointer to a BackendEngine object containing backend proxy's global context,
 *                               such as runtime configuration, memory allocator handles, or system resources.
 *                               Used for accessing backend-specific settings or memory management during message construction.
 *                               Must not be NULL.
 * @param[in]  header            Pointer to a GeneralProxyMsgHeader structure specifying the message header.
 *                               Must not be NULL.
 * @param[in]  payload           Pointer to the const uint8_t buffer containing the message payload.
 *                               Can be NULL only if payload_len is 0.
 * @param[in]  payload_len       Length of the payload in bytes. Must be non-negative and match
 *                               header->payload_len (if header contains payload length field) for consistency.

 * @param[out] result_msg        Double pointer to receive the address of the constructed proxy message.
                                 For MEMORY_ALLOC_SHARED: On success, points to the message location within the shared FIFO RING buffer. 
                                 No caller action is needed for deallocation.
                                 For MEMORY_ALLOC_CALLER: Must point to a pre-allocated buffer. On success, the buffer is populated with 
                                 the complete message. Caller must ensure sufficient size.Must not be NULL.
 * @param[in] ring_buf           Pointer to a struct SharedMemoryPoolQueue. Required and must not be NULL when alloc_mode is MEMORY_ALLOC_SHARED 
                                 (used for FIFO RING buffer operations).
                                 Ignored when alloc_mode is MEMORY_ALLOC_CALLER (can be NULL).
 * @return int                   Returns FRONTEND_PROXY_PROCESS_OK on successful message construction;
 *                               Returns FRONTEND_PROXY_PROCESS_ERROR if any parameter is invalid (e.g., NULL pointers,
 *                               mismatched lengths) 
 *                               Returns FRONTEND_PROXY_PROCESS_AGAIN if shared-memory queue is full (for MEMORY_ALLOC_SHARED)
 */

#if 1
int build_proxy_general_message(FrontendEngine *engine, GeneralProxyMsgHeader *header, 
                                const uint8_t *payload, size_t payload_len, uint8_t **result_msg, 
                                MemoryAllocMode alloc_mode, struct SharedMemoryPoolQueue *ring_buf){
    uint8_t         *msg_buf;
    uint64_t        mem_addr;
    uint16_t        proxy_msg_payload_len;
    ProxyMsgType    outer_msg_type;
    ProxyMsgHeader  *proxy_msg_hdr;
    DevMsgHeader    *dev_hdr;
    StrgyMsgHeader  *strgy_hdr;
    SessMsgHeader   *sess_hdr;
    int             ret, alloc_size, sub_iot_hdr_len;

/*
 * Check the validity of the input parameters.
 */
    if(NULL == header || NULL == result_msg){
        error_print("build_proxy_general_message failed: input(s) for generating proxy message is/are NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == engine){
        error_print("build_proxy_general_message failed: backend engine is NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    outer_msg_type = header->outer_header.proxy_msg_type;
//    header->inner_header.dev_hdr.payload_len;
    

/*
 * Allocate shared-memory for storing the proxy message.
 */
    if(MEMORY_ALLOC_SHARED == alloc_mode){
        if(NULL == ring_buf){
            error_print("build_proxy_general_message failed: MEMORY_ALLOC_SHARED mode requires a non-NULL ring buffer (FIFO queue)!");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }

        utils_print("before SHM_POOL_QUEUE_ALLOC_FROM_HEADER, header = %d, tail = %d, virt addr = %lld\n", ring_buf->header, ring_buf->tail, ring_buf->virt_addr1);
        SHM_POOL_QUEUE_ALLOC_FROM_HEADER(ring_buf, &mem_addr);
        utils_print("after SHM_POOL_QUEUE_ALLOC_FROM_HEADER, header = %d, tail = %d, memaddr = %lld\n", ring_buf->header, ring_buf->tail, mem_addr);

        if(ERROR_SHARED_MEM_ADDR == mem_addr){
            error_print("build_proxy_general_message failed: shared memory FIFO queue is full, cannot allocate new block!\n");
            return FRONTEND_PROXY_PROCESS_AGAIN;
        }

        msg_buf         = (uint8_t *)mem_addr;
        *result_msg     = msg_buf;

//        SHM_POOL_QUEUE_LOOKUP_VIRTADDR(ring_buf, 1, 1, &mem_addr);
//        SHM_POOL_QUEUE_ALLOC_FROM_HEADER(ring_buf, &mem_addr);
    }else if(MEMORY_ALLOC_CALLER == alloc_mode){
/*
 * Frontend protocol should allocate memory dynamically.
 */
        alloc_size = sizeof(ProxyMsgHeader) + header->outer_header.payload_len;
        msg_buf         = malloc(alloc_size);

        if(NULL == msg_buf){
            error_print("build_proxy_general_message failed: insufficient memory for allocation!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }

        *result_msg     = msg_buf;
    }else if(MEMORY_ALLOC_AMPQUEUE  == alloc_mode){
        if(NULL == engine->hyper_tx_queue){
            error_print("build_proxy_general_message failed: HyperAMP TX queue is not initialized!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
        memset(global_amp_tx_buf, 0, sizeof(global_amp_tx_buf));
        msg_buf         = global_amp_tx_buf;
        *result_msg     = msg_buf;
    }else{
        error_print("build_proxy_general_message failed: unsupported allocte mode!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/*
 * Fill the proxy message header.
 */
    proxy_msg_hdr                       = (ProxyMsgHeader *)msg_buf;
    proxy_msg_hdr->version              = header->outer_header.version;
    proxy_msg_hdr->proxy_msg_type       = outer_msg_type;
    proxy_msg_hdr->frontend_sess_id     = header->outer_header.frontend_sess_id;
    proxy_msg_hdr->backend_sess_id      = header->outer_header.backend_sess_id;
    
    utils_print("In %s, version = %d, proxy_msg_type = %d, frontend_sess_id = %d, backend_sess_id = %d, payload_len = %d\n", __func__,
                proxy_msg_hdr->version, proxy_msg_hdr->proxy_msg_type, proxy_msg_hdr->frontend_sess_id, proxy_msg_hdr->backend_sess_id, proxy_msg_hdr->payload_len);
    msg_buf += sizeof(ProxyMsgHeader);
    switch(outer_msg_type) {
        case PROXY_MSG_TYPE_DEV:
            dev_hdr               = &header->inner_header.dev_hdr;
            proxy_msg_payload_len = sizeof(DevMsgHeader);
            utils_print("In %s, before enter build_proxy_dev_message\n", __func__);
            ret = build_proxy_dev_message(dev_hdr, payload, payload_len, &msg_buf);
            break;
        case PROXY_MSG_TYPE_STRGY:
            strgy_hdr             = &header->inner_header.strgy_hdr;
            proxy_msg_payload_len = sizeof(StrgyMsgHeader);
            ret = build_proxy_strgy_message(strgy_hdr, payload, payload_len, &msg_buf);
            break;
        case PROXY_MSG_TYPE_SESS:
            sess_hdr              = &header->inner_header.sess_hdr;
            proxy_msg_payload_len = sizeof(SessMsgHeader);
            utils_print("In %s, before enter build_proxy_sess_message\n",  __func__);
            ret = build_proxy_sess_message(sess_hdr, payload, payload_len, &msg_buf);
            break;
        case PROXY_MSG_TYPE_DATA:
            proxy_msg_payload_len = 0;
            ret = build_proxy_data_message(proxy_msg_hdr, payload, payload_len, &msg_buf);
            utils_print("In %s, after build_proxy_data_message, the return value is %d\n", __func__, ret);
            break;
        case PROXY_MSG_TYPE_IOT:
            proxy_msg_payload_len = 0;
            utils_print("CASE PROXY_MSG_TYPE_IOT, inner_header.iot_hdr.proto_type = %d, address of inner_header.iot_hdr.proto_type = %p\n", 
                         header->inner_header.iot_hdr.proto_type, &header->inner_header.iot_hdr.proto_type);
            if(IOT_PROTO_TYPE_BLUETOOTH == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotBtAddr);
            }else if(IOT_PROTO_TYPE_CAN == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotCanAddr);
            }else if(IOT_PROTO_TYPE_ZIGBEE == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotZigbeeAddr);
            }else if(IOT_PROTO_TYPE_LORA == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotLoraAddr);
            }else if(IOT_PROTO_TYPE_POWERLINK == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotPowerLinkAddr);
            }else if(IOT_PROTO_TYPE_MODBUSTCP == header->inner_header.iot_hdr.proto_type){
                sub_iot_hdr_len = sizeof(IotModbusTcpAddr);
            }else{
                error_print("build_proxy_general_message failed: unsupported IoT protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }

            proxy_msg_payload_len = sizeof(IotMsgHeader) + sub_iot_hdr_len;

            utils_print("In %s, before build_proxy_iot_message, sub_iot_hdr_len = %d, proxy_msg_payload_len = %d\n", __func__, sub_iot_hdr_len, proxy_msg_payload_len);
            ret = build_proxy_iot_message(header, payload, payload_len, &msg_buf);
            utils_print("In %s, after build_proxy_iot_message, the return value is %d\n", __func__, ret);
            break;
        default:
/*
 * Message type is not supported!.
 */
            error_print("build_proxy_general_message failed: message type is not supported!");
            free_shared_mem(engine->mem_pool, mem_addr);
            return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("build_proxy_general_message failed: failed to build proxy message!\n");
//        free_shared_mem(engine->mem_pool, mem_addr);
//        SHM_POOL_QUEUE_HEAD_ROLLBACK(ring_buf);
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/*
 * Compute the payload length and fill it into the corresponding field of the proxy message header.
 */
    proxy_msg_hdr->payload_len = payload_len + proxy_msg_payload_len;

/*
 * In MEMORY_ALLOC_SHARED mode, the build_proxy_general_message function is responsible for
 * enqueuing the constructed message into the shared memory FIFO queue (ring_buf)
 */
#if 0
    if(MEMORY_ALLOC_SHARED == alloc_mode){
        utils_print("before SHMP_QUEUE_ENQUEUE, header = %d, tail = %d\n", ring_buf->header, ring_buf->tail);
        SHMP_QUEUE_ENQUEUE(ring_buf, ret);
        utils_print("after SHMP_QUEUE_ENQUEUE, header = %d, tail = %d\n", ring_buf->header, ring_buf->tail);

        SHM_POOL_QUEUE_HEAD_ROLLBACK(ring_buf);
        utils_print("after SHM_POOL_QUEUE_HEAD_ROLLBACK, header = %d, tail = %d\n", ring_buf->header, ring_buf->tail);
        return ret;
    }
#endif


#if 0
    int debug_cnt;
    utils_print("shared queue capacity = %d, header = %d, tail = %d, block_size = %d\n", ring_buf->capacity, ring_buf->header, ring_buf->tail, ring_buf->block_size);

    debug_cnt = ring_buf->header;

    utils_print("Debug shared memory I/O\n");
    utils_print("virt addr = %lld\n", ring_buf->virt_addr1);
    while(debug_cnt < ring_buf->capacity + 10){
        uint64_t  debug_mem_addr;
        SHM_POOL_QUEUE_ALLOC_FROM_HEADER(ring_buf, &debug_mem_addr);
        utils_print("shared queue header = %d, tail = %d, addr = %lld, diff = %d\n", ring_buf->header, ring_buf->tail, debug_mem_addr, debug_mem_addr - ring_buf->virt_addr1);
        debug_cnt++;
    }
#endif

/*
 * In MEMORY_ALLOC_AMPQUEUE mode, the created message should be pushed into the HyperAMP shared queue.
 */
    if(MEMORY_ALLOC_AMPQUEUE == alloc_mode){
        msg_buf -= sizeof(ProxyMsgHeader);
        utils_print("Before hyperamp_queue_enqueue, the address of the engine->hyper_tx_queue is %p, data region is %p\n", engine->hyper_tx_queue, g_hyper_data_region);
        ret = hyperamp_queue_enqueue(engine->hyper_tx_queue, HYPERAMP_ZONE_ID_SEL4, msg_buf, payload_len + proxy_msg_payload_len + sizeof(ProxyMsgHeader), g_hyper_data_region);

        if(HYPERAMP_OK == ret){
            return FRONTEND_PROXY_PROCESS_OK;
        }else if(HYPERAMP_AGAIN == ret){
            error_print("build_proxy_general_message failed: HyperAMP queue is full!\n");
            return FRONTEND_PROXY_PROCESS_AGAIN;
        }else{
            error_print("build_proxy_general_message failed: failed to push message into the HyperAmp queue!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
    }
    return FRONTEND_PROXY_PROCESS_OK;
}
#endif


/**
 * @brief Frontend proxy message processing main entry
 *
 * @details As the core message distribution function of the frontend proxy, it is responsible for parsing the type field of the input proxy message,
 * and automatically routing to the corresponding specialized processing function according to the message type to realize differentiated processing of different types of proxy messages.
 * Supported message types and their corresponding processing functions are as follows:
 * - Device-related messages: Call frontend_proxy_dev_msg_process
 * - Strategy-related messages: Call frontend_proxy_strgy_msg_process
 * - Session-related messages: Call frontend_proxy_sess_msg_process
 * - Data-related messages: Call frontend_proxy_data_msg_process
 *
 * @param msg Pointer to the proxy message buffer, pointing to a uint8_t array containing the message type identifier and specific message content
 * @return int Processing result status code
 *         - FRONTEND_PROXY_PROCESS_OK: Message distribution succeeded (Note: The specific processing result is guaranteed by the corresponding specialized function)
 *         - FRONTEND_PROXY_PROCESS_ERROR: Message type parsing failed or an error occurred during the distribution process
 *
 * @note 1. The input parameter 'msg' must be non-null and point to valid memory; otherwise, undefined behavior may occur
 *       2. The parsing rule of the message type must be consistent with that of the backend proxy (backend_proxy_msg_process)
 *       3. The return result of the specialized processing function does not affect the return status of the current function; only whether the distribution process is successful is fed back
 */
int frontend_proxy_msg_process(uint8_t *msg){
    printf("In %s-1\n", __func__);
    ProxyMsgHeader *proxy_msg_hdr;
    int proxy_proto_ver, msg_len, ret;
    ProxyMsgType msg_type;
    uint16_t frontend_sess_id, backend_sess_id;
    uint8_t *msg_ptr;

    struct FrontendSession* sess;

    proxy_msg_hdr = (ProxyMsgHeader *)msg;

/*
 * Currently, the backend protocol stack does not differentiate the protocol version, we reserve the protocol version for future extensions.
 */
    proxy_proto_ver     = proxy_msg_hdr->version;
    frontend_sess_id    = proxy_msg_hdr->frontend_sess_id;
    backend_sess_id     = proxy_msg_hdr->backend_sess_id;
    msg_type            = proxy_msg_hdr->proxy_msg_type;
    msg_len             = proxy_msg_hdr->payload_len;

    utils_print("In %s, version = %d, frontend sess id = %d, backend sess id = %d, msg_type = %d, msg_len = %d\n", 
                __func__, proxy_proto_ver, frontend_sess_id, backend_sess_id, msg_type, msg_len);
/*
 * Check the validity of the message type.
 */
    if(!PROXY_MSG_TYPE_VALID(msg_type)){
        error_print("frontend_proxy_msg_process failed: unsupported message type!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }// Unsupported message type.

/*
 * Check the validity of the message length.
 */
    if(!PROXY_MSG_LEN_VALID(msg_type)){
        error_print("frontend_proxy_msg_process failed: message length error!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }// Unsupported message type.

    msg_ptr = (uint8_t *)proxy_msg_hdr;
    msg_ptr += PROXY_MSG_HDR_SIZE;

    if(PROXY_MSG_TYPE_DEV == msg_type){
    /* 
     * The frontend proxy delivers device messages from the frontend admin session to the backend proxy for the backend admin session.
     */
        if (frontend_sess_id != FRONTEND_ADMIN_SESSION_ID || backend_sess_id != BACKEND_ADMIN_SESSION_ID){
            error_print("frontend_proxy_msg_process failed: only admin sessions can deliver and process device messages!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
        ret = frontend_proxy_dev_msg_process(msg_ptr);
    }else if(PROXY_MSG_TYPE_STRGY == msg_type){
    /* 
     * The frontend proxy delivers strategy messages from the frontend admin session to the backend proxy for the backend admin session.
     */
        if (frontend_sess_id != FRONTEND_ADMIN_SESSION_ID || backend_sess_id != BACKEND_ADMIN_SESSION_ID){
            error_print("frontend_proxy_msg_process failed: only admin sessions can deliver and process strategy messages!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
        ret = frontend_proxy_strgy_msg_process(msg_ptr);
    }else if(PROXY_MSG_TYPE_SESS == msg_type){
        printf("In %s-3\n", __func__);
        ret = frontend_proxy_sess_msg_process(frontend_sess_id, backend_sess_id, msg_ptr);
    }else if(PROXY_MSG_TYPE_DATA == msg_type){
/*
 * When msg_type is PROXY_MSG_TYPE_DATA, the frontend_sess_id and backend_sess_id should be checked to determine whether the session (if it exists) 
 * is an application session.
 */
        if(!APP_SESSION_ID_VALID(frontend_sess_id) || !APP_SESSION_ID_VALID(backend_sess_id)){
            error_print("Both the frontend session ID and backend session ID in the proxy data message must pass the application session ID validation!");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
        printf("In %s-4\n", __func__);
        ret = frontend_proxy_data_msg_prosess(frontend_sess_id, backend_sess_id, msg_len, msg_ptr);
    }else if(PROXY_MSG_TYPE_IOT == msg_type){
/*
 * process IoT message.
 */
        printf("In %s-5\n", __func__);
        ret = frontend_proxy_iot_msg_process(frontend_sess_id, backend_sess_id, msg_len, msg_ptr);
    }else{
        error_print("frontend_proxy_msg_process failed: unsupport message type!\n");
        utils_print("msg_type = %d\n", msg_type);
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}



/*
 * Device message processing functions.
 * frontend_proxy_dev_msg_process
 *     |->frontend_proxy_dev_msg_process_ver1
 *         |->frontend_proxy_dev_msg_process_disable_ver1
 *         |->frontend_proxy_dev_msg_process_enable_ver1
 *         |->frontend_proxy_dev_msg_process_query_ver1
 */
int frontend_proxy_dev_msg_process(uint8_t *msg){
    DevMsgHeader *dev_msg_hdr;
    uint16_t version, msg_id, payload_len;
    DevMsgType msg_type;
    ActionType action_type;
    int ret;
    uint8_t *msg_data;

    ret = FRONTEND_PROXY_PROCESS_ERROR;

    dev_msg_hdr = (DevMsgHeader *)msg;

    if(NULL == dev_msg_hdr){
        error_print("frontend_proxy_dev_msg_process() failed: the msg pointer is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
    version = dev_msg_hdr->version;
    msg_type = dev_msg_hdr->msg_type;
    msg_id = dev_msg_hdr->msg_id;
    action_type = dev_msg_hdr->action_type;
    payload_len = dev_msg_hdr->payload_len;
    msg_data = msg + sizeof(DevMsgHeader);

/*    
 * The frontend protocol stack only processes messages where the action_type is ACTION_TYPE_RESPONSE.
 */
    if(ACTION_TYPE_RESPONSE != action_type){
        error_print("frontend_proxy_dev_msg_process() failed: the frontend protocol stack only processes the device message of the ACTION_TYPE_RESPONSE type!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
/*
 * Before processing device messages, the protocol stack should check the validity of parameters.
 *
 * Currently, the protocol stack only processes Version 1 device messages.
 */

    if(PROXY_PROTO_DEV_VERSION_1 == version){
        ret = frontend_proxy_dev_msg_process_ver1(msg_type, msg_id, action_type, payload_len, msg_data);
    } 

    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_dev_msg_process_ver1(uint16_t msg_type, uint16_t msg_id, uint16_t action_type, uint16_t payload_len, uint8_t *msg_payload){
    int corr_len;
    int ret = FRONTEND_PROXY_PROCESS_ERROR;

/* 
 * Check whether the payload length matches the message type and signaling type.
 */
    corr_len = DEV_MSG_PAYLOAD_LEN(msg_type, action_type);

    utils_print("In %s, corr_len = %d, payload_len = %d\n", __func__, corr_len, payload_len);

    if(PROXY_MSG_INVALID_LEN == corr_len || corr_len != payload_len){
            error_print("frontend_proxy_dev_msg_process_ver1() failed: invalid msg_type or payload length mismatch!");
            return FRONTEND_PROXY_PROCESS_ERROR;
    }

    switch(msg_type) {
        case DEV_MSG_DISABLE:
            ret = frontend_proxy_dev_msg_process_disable_ver1(payload_len, msg_payload);
            break;
        case DEV_MSG_ENABLE:
            ret = frontend_proxy_dev_msg_process_enable_ver1(payload_len, msg_payload);
            break;
        case DEV_MSG_QUERY:
            ret = frontend_proxy_dev_msg_process_query_ver1(payload_len, msg_payload);
            break;
        default:
/*
 * Nothing to do, because the validation of the msg_type is checked before.
 */
            break;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_dev_msg_process_disable_ver1(uint16_t payload_len, uint8_t *msg_payload){
    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_dev_msg_process_enable_ver1(uint16_t payload_len, uint8_t *msg_payload){
    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_dev_msg_process_query_ver1(uint16_t payload_len, uint8_t *msg_payload){
    return FRONTEND_PROXY_PROCESS_OK;
}



/*
 * Strategy message processing functions.
 * frontend_proxy_strgy_msg_process
 *     |->frontend_proxy_strgy_msg_process_ver1
 *         |->frontend_proxy_strgy_msg_process_set_ver1
 *         |->frontend_proxy_strgy_msg_process_query_ver1
 */

int frontend_proxy_strgy_msg_process(uint8_t *msg){
    StrgyMsgHeader *strgymsg_hdr;
    uint16_t version, msg_id, payload_len;
    StrgyMsgType msg_type;
    ActionType action_type;
    int ret;
    uint8_t *msg_data;

    ret = FRONTEND_PROXY_PROCESS_ERROR;

    if(NULL == strgymsg_hdr){
        error_print("frontend_proxy_strgy_msg_process() failed: the msg pointer is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    version = strgymsg_hdr->version;
    msg_type = strgymsg_hdr->msg_type;
    msg_id = strgymsg_hdr->msg_id;
    action_type = strgymsg_hdr->action_type;
    payload_len = strgymsg_hdr->payload_len;
    msg_data = msg + sizeof(StrgyMsgHeader);


/*    
 * The frontend protocol stack only processes messages where the action_type is ACTION_TYPE_RESPONSE.
 */
    if(ACTION_TYPE_RESPONSE != action_type){
        error_print("frontend_proxy_strgy_msg_process() failed: the frontend protocol stack only processes the strategy messages of type ACTION_TYPE_RESPONSE!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/*
 * Before processing device messages, the protocol stack should check the validity of parameters.
 *
 * Currently, the protocol stack only processes Version 1 strategy messages.
 */
    if(PROXY_PROTO_STRGY_VERSION_1 == version){
        ret = frontend_proxy_strgy_msg_process_ver1(msg_type, msg_id, action_type, payload_len, msg_data);
    } 

    return FRONTEND_PROXY_PROCESS_OK;
}

int frontend_proxy_strgy_msg_response(uint8_t *msg);


int frontend_proxy_strgy_msg_process_ver1(uint16_t msg_type, uint16_t msg_id, uint16_t action_type, uint16_t payload_len, uint8_t *msg_payload){
    int corr_len;
    int ret = FRONTEND_PROXY_PROCESS_ERROR;

/* 
 * Check whether the payload length matches the message type and signaling type.
 */
    corr_len = STRGY_MSG_PAYLOAD_LEN(msg_type, action_type);

    utils_print("In %s, corr_len = %d, payload_len = %d\n", __func__, corr_len, payload_len);

    if(PROXY_MSG_INVALID_LEN == corr_len || corr_len != payload_len){
        error_print("backend_proxy_strgy_msg_process_ver1() failed: invalid msg_type or payload length mismatch!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
    switch(msg_type) {
        case STRGY_MSG_SET:
            ret = frontend_proxy_strgy_msg_process_set_ver1(payload_len, msg_payload);
            break;
        case STRGY_MSG_QUERY:
            ret = frontend_proxy_strgy_msg_process_query_ver1(payload_len, msg_payload);
            break;
        default:
/*
 * Nothing to do, because the validation of the msg_type is checked before.
 */
            break;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_strgy_msg_process_set_ver1(uint16_t payload_len, uint8_t *msg_payload){
    return FRONTEND_PROXY_PROCESS_OK;
}



int frontend_proxy_strgy_msg_process_query_ver1(uint16_t payload_len, uint8_t *msg_payload){
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Processes session messages in the frontend proxy
 * @details This function serves as the core handler for session-related communication between front and backend proxies.
 *          It handles general session message processing by parsing the incoming message, coordinating with the specified
 *          frontend and backend sessions, and executing appropriate operations based on message content.
 *          The processing follows a hierarchical call structure:
 *          frontend_proxy_sess_msg_process
 *              |-> frontend_proxy_sess_msg_process_ver1
 *                  |-> frontend_proxy_sess_msg_process_active_create_ver1
 *                  |-> frontend_proxy_sess_msg_process_close_ver1
 * @param[in] frontend_sess_id 16-bit identifier of the frontend session, used to map the message to
 *                             the corresponding frontend session context.
 * @param[in] backend_sess_id 16-bit identifier of the backend session, used to associate the message
 *                            with the relevant backend session state and resources.
 * @param[in] msg Pointer to the session message data to be processed, containing the complete
 *                message content (e.g., operation type, parameters, metadata). Must not be NULL.
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK on successful message processing;
 *         returns FRONTEND_PROXY_PROCESS_ERROR if message parsing fails, session identifiers are invalid, or
 *         the requested operation cannot be completed.
 */
int frontend_proxy_sess_msg_process(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint8_t *msg){
    SessMsgHeader *sess_msg_hdr;
    uint16_t version, payload_len;
    SessMsgType msg_type;
    ActionType action_type;
    SessIpProtoVersion ip_version;
    int ret;
    uint8_t *msg_data;

    ret = FRONTEND_PROXY_PROCESS_ERROR;

    sess_msg_hdr = (SessMsgHeader *)msg;

    utils_print("In %s\n", __func__);

    if(NULL == sess_msg_hdr){
        error_print("backend_proxy_sess_msg_process() failed: the msg pointer is NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    version         = sess_msg_hdr->version;
    msg_type        = sess_msg_hdr->msg_type;
    action_type     = sess_msg_hdr->action_type;
    ip_version      = sess_msg_hdr->ip_version;
    payload_len     = sess_msg_hdr->payload_len;
    msg_data        = msg + sizeof(SessMsgHeader);

    utils_print("version = %d, msg_type = %d, action type = %d, ip version = %d, payload len = %d, address = %p\n", 
                version, msg_type, action_type, ip_version, payload_len, &sess_msg_hdr);


#if 0
/*    
 * The frontend protocol stack only processes messages where the action_type is ACTION_TYPE_RESPONSE.
 */
    if(ACTION_TYPE_RESPONSE != action_type){
        error_print("frontend_proxy_sess_msg_process() failed: the frontend protocol stack only processes session messages of type ACTION_TYPE_RESPONSE!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
#endif

/*
 * Before processing device messages, the protocol stack should check the validity of parameters.
 *
 * Currently, the protocol stack only processes Version 1 device messages.
 */

#if 0
    SessParaIPv4 *debug_hdr     = (SessParaIPv4 *)msg_data;
    SessIPv4Params *debug_hdr2  = (SessIPv4Params *)msg_data;
    utils_print("In %s, type is SessParaIPv4, dev_id = %d, trans_proto = %d, port = %d\n", __func__, debug_hdr->dev_id, debug_hdr->trans_proto, debug_hdr->port);
    utils_print("In %s, type is SessIPv4Params, devive_selection = %d, transport_layer_proto = %d\n", 
                __func__, debug_hdr2->device_selection, debug_hdr2->transport_layer_proto);
#endif

    if(PROXY_PROTO_SESS_VERSION_1 == version){
        ret = frontend_proxy_sess_msg_process_ver1(frontend_sess_id, backend_sess_id, msg_type, action_type, ip_version, payload_len, msg_data);
    } 

    return ret;
}


int frontend_proxy_sess_msg_response(uint8_t *msg);


int frontend_proxy_sess_msg_process_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t msg_type, 
                                        uint16_t action_type, uint16_t ip_version, uint16_t payload_len, 
                                        uint8_t *msg_payload){
    int corr_len, ret;

/* 
 * Check whether the payload length matches the message type and signaling type.
 */

    utils_print("In %s\n", __func__);

    corr_len = SESS_MSG_PAYLOAD_LEN(msg_type, action_type, ip_version);

    utils_print("corr_len = %d, payload_len = %d\n", corr_len, payload_len);

    if(PROXY_MSG_INVALID_LEN == corr_len || corr_len != payload_len){
            error_print("frontend_proxy_sess_msg_process_ver1 failed: invalid msg_type or payload length mismatch!");
            return FRONTEND_PROXY_PROCESS_ERROR;
    }

    switch(msg_type) {
        case SESS_MSG_CREATE:
            if(ACTION_TYPE_RESPONSE == action_type){
                ret = frontend_proxy_sess_msg_process_active_create_ver1(frontend_sess_id, backend_sess_id, ip_version, payload_len, msg_payload);
            }else if(ACTION_TYPE_COMMAND == action_type){
                ret = frontend_proxy_sess_msg_process_passive_create_ver1(frontend_sess_id, backend_sess_id, ip_version, payload_len, msg_payload);
            }else{
                error_print("frontend_proxy_sess_msg_process_ver1 failed: unsupported action type (neither ACTION_TYPE_RESPONSE nor ACTION_TYPE_COMMAND)!\n");
                ret = FRONTEND_PROXY_PROCESS_ERROR;
            }

            break;
        case SESS_MSG_CLOSE:
            ret = frontend_proxy_sess_msg_process_close_ver1(frontend_sess_id, backend_sess_id, payload_len, msg_payload);
            break;
        default:
/*
 * Nothing to do, because the validation of the msg_type is checked before.
 */
            break;
    }

    return ret;
}


/**
 * @brief Processes version 1 of **active** session creation messages in the frontend proxy
 * @details This function handles the processing logic for version 1 **active** session creation messages in the frontend proxy layer.
 * It parses the incoming message payload, performs parameter validation (e.g., payload length check, IP version validity),
 * and executes frontend-side **active** session creation operations. It is responsible for establishing the mapping between
 * the frontend session and backend session, and ensuring the correct transmission of **active** session creation details
 * for the version 1 message format.
 * @param[in] frontend_sess_id 16-bit identifier of the frontend session, uniquely marks the session on the frontend side
 * for associating with client requests and backend session mapping
 * @param[in] backend_sess_id 16-bit identifier of the backend session, used by the frontend to track and associate with
 * the corresponding backend-side session instance
 * @param[in] ip_version 16-bit value indicating the IP protocol version (e.g., IPv4 = 4, IPv6 = 6) adopted by the current session
 * @param[in] payload_len 16-bit length of the message payload (in bytes), specifies the valid data size in the msg_payload buffer
 * @param[in] msg_payload Pointer to the buffer storing the version 1 **active** session creation message payload, containing
 * detailed configuration parameters required for **active** session establishment
 * @return int Execution result: Typically returns FRONTEND_PROXY_PROCESS_OK on successful processing (including payload parsing,
 * validation passed, and **active** session creation completed), or FRONTEND_PROXY_PROCESS_ERROR if parameter invalidation,
 * payload mismatch, or **active** session creation failure occurs.
 */
int frontend_proxy_sess_msg_process_active_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload){
    return __frontend_proxy_sess_msg_process_active_create_ver1(frontend_sess_id, backend_sess_id, ip_version, payload_len, msg_payload);
}




int __frontend_proxy_sess_msg_process_active_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload){
    struct FrontendSessionPool          *pool;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    struct FrontendSession              *sess;
    SessOpRespData                      *resp_data;
#if 0
    SessParaIPv4 *para_ipv4;
    SessParaIPv6 *para_ipv6;
    IPv4PortTuple *ipv4_port_tuple;
    IPv6PortTuple *ipv6_port_tuple;
    struct IPv4Address *ipv4_addr;
    struct IPv6Address *ipv6_addr;
    struct SessMsgPara sess_para;
#endif
    bool ip_ver_valid = true;
    int ret;

    utils_print("In %s\n", __func__);
/*
 * The main body of the session creation procedure lies in the function which the create_sess_step2 pointer points to.
 * In __frontend_proxy_sess_msg_process_active_create_ver1, this function parses the session parameters and calls the function pointed to by the create_sess_step2 pointer to establish a new session.
 */
    pool = frontend_get_high_speed_pool();

    if(NULL == pool || NULL == pool->ops || NULL == pool->ops->search_sess){
        error_print("__frontend_proxy_sess_msg_process_active_create_ver1 failed: the high-speed session pool is not initialized correctly!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess_pool_ops   = pool->ops;
    sess            = sess_pool_ops->search_sess(pool, frontend_sess_id);

    if(NULL == sess){
        error_print("__frontend_proxy_sess_msg_process_active_create_ver1 failed: search session failed, no matching frontend session found in high-speed session pool.\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(NULL == sess_pool_ops->create_sess_step2){
        error_print("__frontend_proxy_sess_msg_process_active_create_ver1 failed: high-speed session pool operation function 'create_sess_step2' is not initialized (NULL pointer). Session creation step 2 cannot be executed.\n");
    }

    resp_data   = (SessOpRespData *)msg_payload;
    ret         = sess_pool_ops->create_sess_step2(pool, sess, backend_sess_id, resp_data);

    if(FRONTEND_PROXY_PROCESS_OK == ret){
        sess->event_callback(sess, FRONTEND_SESS_EVENT_CONN);
    }else{
        sess->event_callback(sess, FRONTEND_SESS_EVENT_ABNORMAL);
    }


    return ret;
}


/**
 * @brief Processes version 1 of **passive** session creation messages in the frontend proxy
 * @details This function handles the processing logic for version 1 **passive** session creation messages in the frontend proxy layer.
 * It parses the incoming message payload, performs parameter validation (e.g., payload length check, IP version validity),
 * and executes frontend-side **passive** session creation operations. It is responsible for establishing the mapping between
 * the frontend session and backend session, and ensuring the correct transmission of **passive** session creation details
 * for the version 1 message format.
 * @param[in] frontend_sess_id 16-bit identifier of the frontend session, which is specifically **FRONTEND_HANDOVER_SESSION_ID**
 * for associating with client requests and backend session mapping during passive session establishment
 * @param[in] backend_sess_id 16-bit identifier of the backend session, used by the frontend to track and associate with
 * the corresponding backend-side session instance
 * @param[in] ip_version 16-bit value indicating the IP protocol version (e.g., IPv4 = SESS_IPV4_PROTO, IPv6 = SESS_IPV6_PROTO) adopted 
 *             by the current session
 * @param[in] payload_len 16-bit length of the message payload (in bytes), specifies the valid data size in the msg_payload buffer
 * @param[in] msg_payload Pointer to the buffer storing the version 1 **passive** session creation message payload, containing
 *            detailed configuration parameters required for **passive** session establishment
 * @return int Execution result: Typically returns FRONTEND_PROXY_PROCESS_OK on successful processing (including payload parsing,
 *         validation passed, and **passive** session creation completed), or FRONTEND_PROXY_PROCESS_ERROR if parameter invalidation,
 *         payload mismatch, or **passive** session creation failure occurs.
 */
int frontend_proxy_sess_msg_process_passive_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload){
    return __frontend_proxy_sess_msg_process_passive_create_ver1(frontend_sess_id, backend_sess_id, ip_version, payload_len, msg_payload);
}


int __frontend_proxy_sess_msg_process_passive_create_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t ip_version, uint16_t payload_len, uint8_t *msg_payload){
    FrontendEngine                      *eng;
    struct SharedMemoryPoolQueue        *rx_queue, *tx_queue;
    struct FrontendSessionQueue         *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession              *new_sess = NULL;
    struct FrontendSessionPool          *sess_pool;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    GeneralProxyMsgHeader               proxy_msg_hdr;
    struct SessMsgPara                  msg_para;
    SessParaIPv4                        *para_ipv4;
    SessParaIPv6                        *para_ipv6;
    IPv4PortTuple                       *ipv4_port_tuple;
    IPv6PortTuple                       *ipv6_port_tuple;
    SessOpRespData                      resp_data;
    struct IPv4Address                  *ipv4_addr;
    uint8_t                             *proxy_msg;
    uint32_t                            msg_size;
    int                                 new_sess_id = 0;
    int                                 ret = FRONTEND_PROXY_PROCESS_OK;

    eng = frontend_get_global_engine();

    if(NULL == eng || NULL == eng->sess_pool || NULL == eng->sess_pool->ops){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: frontend engine instance has not been successfully initialized!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess_pool       = eng->sess_pool;
    sess_pool_ops   = sess_pool->ops;

    if(SESS_IPV4_PROTO != ip_version){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: IP protocol version not supported, only IPv4 is supported currently, \ 
                     IPv6 support will be expanded in the future!\n");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_PARAMETER_INVALID;
        goto create_sess_error;
    }

/*
 * For the session-creation command sent from the backend to the frontend,
 * the valid value of the frontend session ID in the message shall be FRONTEND_HANDOVER_SESSION_ID (0xFF).
 */
    if(FRONTEND_HANDOVER_SESSION_ID != frontend_sess_id){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: invalid session creation command!\n");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_PARAMETER_INVALID;
        goto create_sess_error;
    }


/*
 * Alloc resource for the new creating session. 
 */
    new_sess = malloc(sizeof(struct FrontendSession));

    if(NULL == new_sess){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: failed to allocate memory!");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_RESOURCE_INSUFFICIENT;
        goto create_sess_error; 
    }

    new_sess->event_callback = default_session_event_callback;

    new_sess_id = allocate_id(&sess_pool->id_queue);
    if(0 == new_sess_id){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: failed to allocate session ID!");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_RESOURCE_INSUFFICIENT;
        goto create_sess_error;
    }

    proxy_msg_hdr.outer_header.frontend_sess_id = new_sess_id;


/*
 * Fill the fields of the SessMsgPara structure instance.
 * This function/module is responsible for populating all required parameters into the SessMsgPara struct,
 * which provides core data support for subsequent session creation operations.
 */
    para_ipv4 = (SessParaIPv4 *)msg_payload;
    memset(&msg_para, 0, sizeof(struct SessMsgPara));
    msg_para.frontend_sess_id                       = frontend_sess_id;
    msg_para.backend_sess_id                        = backend_sess_id;
    msg_para.dev_id                                 = para_ipv4->dev_id;
    msg_para.ip_version                             = ip_version;
    msg_para.trans_proto                            = para_ipv4->trans_proto;
    msg_para.ip_port_tuple.ipv4_port_tuple.port     = para_ipv4->port;

    ipv4_addr = &msg_para.ip_port_tuple.ipv4_port_tuple.ipv4_addr;
    memcpy(ipv4_addr, &para_ipv4->ipv4_addr, sizeof(struct IPv4Address));

    ret = sess_pool_ops->create_sess_passive(sess_pool, new_sess, &msg_para);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("__frontend_proxy_sess_msg_process_passive_create_ver1 failed: ");
        goto create_sess_error;
    }

    return ret;

create_sess_error:
/*
 * Reclaim resources.
 */
    

    if(NULL == new_sess){
        free(new_sess);
    }

    if(0 != new_sess_id){
        release_id(&sess_pool->id_queue, new_sess_id);
    }

    return FRONTEND_PROXY_PROCESS_ERROR;
}

/**
 * @brief Processes version 1 of session close messages in the frontend proxy
 * @details This function handles the processing logic for version 1 session close messages in the frontend proxy layer.
 * It parses the message payload, validates the validity of frontend and backend session identifiers,
 * executes frontend-side session termination operations, and cleans up associated resources (e.g., session context,
 * connection mappings). It is specifically designed for the version 1 session close message format,
 * coordinating the termination of the frontend-backend associated session and ensuring proper resource release.
 * @param[in] frontend_sess_id 16-bit identifier of the frontend session, used to locate and target the frontend session to be closed
 * @param[in] backend_sess_id 16-bit identifier of the backend session, used to associate and notify the corresponding backend session for termination
 * @param[in] payload_len 16-bit length of the message payload (in bytes), indicating the valid data size in the msg_payload buffer
 * @param[in] msg_payload Pointer to the session close message payload, containing detailed parameters for the close operation (e.g., termination reason). Must not be NULL.
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK on successful processing of the close message, session termination, and resource cleanup;
 * Returns FRONTEND_PROXY_PROCESS_ERROR if payload is invalid, session identifiers are incorrect, parsing fails, or session closure/resource cleanup encounters errors.
 */
int frontend_proxy_sess_msg_process_close_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t payload_len, uint8_t *msg_payload){
    return __frontend_proxy_sess_msg_process_close_ver1(frontend_sess_id, backend_sess_id, payload_len, msg_payload);
}



int __frontend_proxy_sess_msg_process_close_ver1(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t payload_len, uint8_t *msg_payload){
    struct FrontendSessionPool          *pool;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    struct FrontendSession              *sess;
    SessOpRespData                      *resp_data;
    uint8_t                             status, code;

    pool = frontend_get_high_speed_pool();

    if(NULL == pool || NULL == pool->ops || NULL == pool->ops->search_sess){
        error_print("__frontend_proxy_sess_msg_process_close_ver1 failed: the high-speed session pool is not initialized correctly!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess_pool_ops   = pool->ops;
    sess            = sess_pool_ops->search_sess(pool, frontend_sess_id);

    if(NULL == sess){
        error_print("__frontend_proxy_sess_msg_process_close_ver1 failed: search session failed, no matching frontend session found in high-speed session pool.\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    resp_data   = (SessOpRespData *)msg_payload;

    sess_pool_ops->close_sess_step2(pool, sess, resp_data);

#if 0
    resp_data   = (SessOpRespData *)msg_payload;
    status      = resp_data->status;
    
    if(SESS_OP_STATUS_SUCCESS != status){
        utils_print("In %s, the session creating procedurec failed, the error code is %d\n", __func__, code);
        sess->event_callback(sess, FRONTEND_SESS_EVENT_ABNORMAL);
    }else{
        sess->event_callback(sess, FRONTEND_SESS_EVENT_CLOSE);
    }
#endif

    sess_pool_ops->delete_sess(pool, sess);

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Processes proxy data messages for frontend proxy (between frontend and backend sessions)
 * This function handles the processing of data messages that need to be proxied between a frontend session and its
 * corresponding backend session on the frontend proxy side. It involves message validation, protocol format adaptation,
 * and routing to the target session (frontend or backend) based on the provided session identifiers and message content,
 * ensuring seamless data transmission across the proxy link.
 * @param frontend_sess_id Unique identifier of the frontend session (source/destination of the message)
 * @param backend_sess_id Unique identifier of the backend session (counterpart session for proxying)
 * @param data_len Length of the message data in bytes (specifies valid range of the msg buffer)
 * @param msg Pointer to the message data buffer (uint8_t array) to be processed/proxied
 * @return int Processing result status:
 * FRONTEND_PROXY_PROCESS_OK: Message processed and proxied successfully
 * FRONTEND_PROXY_PROCESS_ERROR: Failed to process or proxy the message (e.g., invalid session IDs,
 * invalid message format, protocol adaptation failure, or forwarding failure)
 * @note 1. The message buffer (msg) is assumed to contain valid data conforming to the proxy protocol; its length
 * should match the actual data length specified by data_len to avoid out-of-bounds access;
 * Callers must ensure frontend_sess_id and backend_sess_id refer to active, valid sessions (the frontend
 * session is managed by the frontend proxy, and the backend session is the associated counterpart),
 * otherwise processing errors will occur;
 * This function does not take ownership of the msg buffer; the caller is responsible for managing its
 * lifecycle (e.g., allocation and release) to prevent memory leaks.
 */
int frontend_proxy_data_msg_prosess(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t data_len, uint8_t *msg){
/*
 * STEP 1. Search for the destination frontend session instance in the session pool using backend_sess_id. If it fails to find
 * the appropriate session instance, frontend_proxy_data_msg_process shall return FRONTEND_PROXY_PROCESS_ERROR;
 * otherwise, proceed to STEP 2.
 * STEP 2. Construct a struct SessMsgSeg object, bind the data message to this SessMsgSeg object, and then link this SessMsgSeg object
 * to the msg_b2f queue of the session instance.
 * STEP 3. Link the frontend session instance to the queue_b2f of the session pool instance to which the session instance belongs.
 * The frontend proxy protocol will process all sessions in queue_b2f and forward all data messages in each msg_b2f queue after
 * it receives all the data messages in the shared-memory queue. This procedure exists outside frontend_proxy_data_msg_process;
 * we just make a note here to help readers maintain a consistent understanding.
 */
    struct FrontendEngine_           *eng;
    struct FrontendSessionPool       *s_pool;
    struct FrontendSessionPoolOps    *ops;
    struct FrontendSession           *sess;
    struct SharedMemoryPool         *mem_pool;
    struct SessMsgSeg               *msg_seg;
    int ret;

    eng = frontend_get_global_engine();

    if(NULL == eng || NULL == eng->sess_pool ||  NULL == eng->sess_pool->ops){
        error_print("frontend_proxy_data_msg_prosess failed: eng, eng->sess_pool,  or eng->sess_pool->ops is NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    s_pool      = eng->sess_pool;
    ops         = eng->sess_pool->ops;
    mem_pool    = eng->mem_pool;

    if(NULL == ops->search_sess){
        error_print("frontend_proxy_data_msg_prosess failed: ops->search_sess (session searching function) is not initialized!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

        sess = ops->search_sess(s_pool, frontend_sess_id);

    if(NULL == sess){
        error_print("frontend_proxy_data_msg_prosess failed: no frontend session found for the specified frontend_sess_id!");
        return FRONTEND_PROXY_PROCESS_ERROR;   
    }

    msg_seg = sess_msg_seg_alloc(data_len, SESS_MSG_SEG_DYNAMIC_ALLOC, msg, mem_pool);
        
    if(NULL == msg_seg){
        error_print("frontend_sess_send failed: insufficient memory for allocating message segment instance!\n");
        return FRONTEND_PROXY_PROCESS_ERROR; 
    }

    utils_print("In %s, after sess_msg_seg_alloc\n", __func__);
    memcpy(msg_seg->data, msg, data_len);
/*
 * Insert the message segment into the back-to-front message queue.
 */
    SESS_MSG_SEG_INSERT_QUEUE(sess, msg_seg, b2f);
    utils_print("In %s, after SESS_MSG_SEG_INSERT_QUEUE\n", __func__);

    FRONTEND_SESS_LINK_TO_QUEUE(sess, b2f);

    utils_print("In %s, after FRONTEND_SESS_LINK_TO_QUEUE\n", __func__);

    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_proxy_data_msg_recv(struct FrontendSession *sess, uint8_t *msg);
int frontend_proxy_data_msg_send(struct FrontendSession *sess, uint8_t *msg);


/**
 * @brief Unified entry function for IoT proxy message processing
 *
 * @details This function serves as the top-level unified entry for all IoT proxy message handling.
 *          It processes messages transmitted from the backend to the frontend through the HyperAMP channel.
 *          It parses message headers, validates parameters and message integrity,
 *          and distributes messages to corresponding protocol-specific processing handlers.
 *
 * @param frontend_sess_id Frontend session ID for message interaction
 * @param backend_sess_id Backend session ID for IoT communication matching
 * @param msg_len Total length of the received IoT proxy message
 * @param msg Pointer to the complete IoT proxy message data
 *
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: Message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Processing failed (invalid parameter/header/type/length)
 *
 * @note This is the core unified entry for all IoT-type proxy messages
 * @note Messages are transmitted from backend to frontend via HyperAMP channel
 * @note Consists of ProxyMsgHeader, IotMsgHeader and protocol-specific payload data
 * @note Validates input parameters, message length and header integrity
 * @note Extracts and uses protocol type to dispatch to corresponding IoT processing logic
 * @note Adopts consistent error code standards with other proxy message modules
 *
 * @warning Ensure the input message pointer and length are valid before invocation
 * @warning Session IDs must match the established frontend-backend communication link
 */
int frontend_proxy_iot_msg_process(uint16_t frontend_sess_id, uint16_t backend_sess_id, uint16_t msg_len, uint8_t *msg){
    IotMsgHeader *iot_msg_hdr;
    uint8_t      *iot_data;
    int          ret;

    iot_msg_hdr = (IotMsgHeader *)msg;

    if(NULL == iot_msg_hdr){
        error_print("frontend_proxy_iot_msg_process failed: msg pointer should not be NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    iot_data = msg + sizeof(IotMsgHeader);

    switch(iot_msg_hdr->proto_type){
        case IOT_PROTO_TYPE_BLUETOOTH:
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotBtAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for Bluetooth protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }

            ret = frontend_proxy_bluetooth_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        case IOT_PROTO_TYPE_CAN:
            utils_print("CAN message length = %d\n", msg_len);
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotCanAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for CAN protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }
            utils_print("Before process CAN message\n");
            ret = frontend_proxy_can_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        case IOT_PROTO_TYPE_ZIGBEE:
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotZigbeeAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for ZigBee protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }

            ret = frontend_proxy_zigbee_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        case IOT_PROTO_TYPE_LORA:
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotLoraAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for LoRa protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }

            ret = frontend_proxy_lora_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        case IOT_PROTO_TYPE_POWERLINK:
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotPowerLinkAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for PowerLink protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }
            ret = frontend_proxy_powerlink_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        case IOT_PROTO_TYPE_MODBUSTCP:
            if(msg_len < sizeof(IotMsgHeader)+ sizeof(IotModbusTcpAddr)){
                error_print("frontend_proxy_iot_msg_process failed: invalid msg_len for ModbusTCP protocol!\n");
                return FRONTEND_PROXY_PROCESS_ERROR;
            }
            ret = frontend_proxy_modbustcp_msg_process(frontend_sess_id, backend_sess_id, iot_msg_hdr, iot_data);
            break;
        default:
            error_print("frontend_proxy_iot_msg_process failed: unsupported protocol type!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return ret;
}


/**
 * @brief Bluetooth-specific IoT proxy message processing
 *
 * Processes Bluetooth IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses Bluetooth address (IotAddr.bt_addr) from IoT protocol data
 * - Validates Bluetooth-specific parameters (MAC/port/connection interval)
 * - Dispatches to bluetooth_send_to_remote/bluetooth_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to Bluetooth protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: Bluetooth message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid Bluetooth addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend Bluetooth messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates Bluetooth session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_bluetooth_msg_process(uint16_t frontend_sess_id,
                                         uint16_t backend_sess_id,
                                         IotMsgHeader *iot_header,
                                         uint8_t *iot_data){
    utils_print("In %s\n", __func__);
    IoTFrontendSession *iot_sess;
    IotBtAddr           *bt_addr;
    uint8_t             *iot_payload;
    IotMsgBuffer        iot_msg_buf;
    int                 ret;

    if(NULL == iot_header || NULL == iot_data){
        error_print("frontend_proxy_bluetooth_msg_process failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("iot_header->payload_len = %d\n", iot_header->payload_len);

    if(iot_header->payload_len <= sizeof(IotBtAddr)){
        error_print("frontend_proxy_bluetooth_msg_process failed: invalid iot_header->payload_len!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(NULL == frontend_bluetooth_sess){
        error_print("frontend_proxy_bluetooth_msg_process failed: bluetooth session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    iot_sess = frontend_bluetooth_sess;
    (void)iot_sess;
    
    bt_addr         = (IotBtAddr *)iot_data;
    iot_payload     = iot_data + sizeof(IotBtAddr);

    (void)bt_addr;
    (void)iot_payload;

    iot_msg_buf.addr.addr_type = IOT_PROTO_TYPE_BLUETOOTH;
    iot_msg_buf.data           = iot_payload;
    iot_msg_buf.len            = iot_header->payload_len - sizeof(sizeof(IotBtAddr));
    memcpy(&iot_msg_buf.addr.addr_info.bt_addr, bt_addr, sizeof(IotBtAddr));

    ret = iot_sess->recv_from_backend(iot_sess, &iot_msg_buf, iot_payload, iot_header->payload_len - sizeof(sizeof(IotBtAddr)));


    return ret;
}

/**
 * @brief CAN-specific IoT proxy message processing
 *
 * Processes CAN IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses CAN address (IotAddr.can_addr) from IoT protocol data
 * - Validates CAN-specific parameters (can_id/frame_type/baudrate)
 * - Dispatches to can_send_to_remote/can_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to CAN protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: CAN message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid CAN addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend CAN messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates CAN session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_can_msg_process(uint16_t frontend_sess_id,
                                   uint16_t backend_sess_id,
                                   IotMsgHeader *iot_header,
                                   uint8_t *iot_data){
    utils_print("In %s\n", __func__);
    IoTFrontendSession *iot_sess;
    IotCanAddr          *can_addr;
    uint8_t             *iot_payload;
    IotMsgBuffer        iot_msg_buf;
    int                 ret;

    if(NULL == iot_header || NULL == iot_data){
        error_print("frontend_proxy_can_msg_process failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(iot_header->payload_len <= sizeof(IotCanAddr)){
        error_print("frontend_proxy_can_msg_process failed: invalid iot_header->payload_len!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    if(NULL == frontend_can_sess){
        error_print("frontend_proxy_can_msg_process failed: CAN session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    iot_sess = frontend_can_sess;
    (void)iot_sess;
    
    can_addr         = (IotCanAddr *)iot_data;
    iot_payload     = iot_data + sizeof(IotCanAddr);

    iot_msg_buf.addr.addr_type = IOT_PROTO_TYPE_CAN;
    iot_msg_buf.data           = iot_payload;
    iot_msg_buf.len            = iot_header->payload_len - sizeof(sizeof(IotCanAddr));
    memcpy(&iot_msg_buf.addr.addr_info.can_addr, can_addr, sizeof(IotCanAddr));

    ret = iot_sess->recv_from_backend(iot_sess, &iot_msg_buf, iot_payload, iot_header->payload_len - sizeof(sizeof(IotCanAddr)));

    return ret;
}

/**
 * @brief ZigBee-specific IoT proxy message processing
 *
 * Processes ZigBee IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses ZigBee address (IotAddr.zigbee_addr) from IoT protocol data
 * - Validates ZigBee-specific parameters (short_addr/mac/channel/pan_id)
 * - Dispatches to zigbee_send_to_remote/zigbee_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to ZigBee protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: ZigBee message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid ZigBee addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend ZigBee messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates ZigBee session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_zigbee_msg_process(uint16_t frontend_sess_id,
                                     uint16_t backend_sess_id,
                                     IotMsgHeader *iot_header,
                                     uint8_t *iot_data){
    

    if(NULL == frontend_zigbee_sess){
        error_print("frontend_proxy_zigbee_msg_process failed: ZigBee session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief LoRa-specific IoT proxy message processing
 *
 * Processes LoRa IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses LoRa address (IotAddr.lora_addr) from IoT protocol data
 * - Validates LoRa-specific parameters (dev_addr/freq/spreading_factor)
 * - Dispatches to lora_send_to_remote/lora_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to LoRa protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: LoRa message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid LoRa addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend LoRa messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates LoRa session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_lora_msg_process(uint16_t frontend_sess_id,
                                   uint16_t backend_sess_id,
                                   IotMsgHeader *iot_header,
                                   uint8_t *iot_data){
    
    if(NULL == frontend_lora_sess){
        error_print("frontend_proxy_lora_msg_process failed: LoRa session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief PowerLink-specific IoT proxy message processing
 *
 * Processes PowerLink IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses PowerLink address (IotAddr.powerlink_addr) from IoT protocol data
 * - Validates PowerLink-specific parameters (node_id/pdo_id/cycle_ms)
 * - Dispatches to powerlink_send_to_remote/powerlink_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to PowerLink protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: PowerLink message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid PowerLink addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend PowerLink messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates PowerLink session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_powerlink_msg_process(uint16_t frontend_sess_id,
                                        uint16_t backend_sess_id,
                                        IotMsgHeader *iot_header,
                                        uint8_t *iot_data){

    if(NULL == frontend_powerlink_sess){
        error_print("frontend_proxy_powerlink_msg_process failed: PowerLink session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    return FRONTEND_PROXY_PROCESS_OK;
}

/**
 * @brief ModbusTCP-specific IoT proxy message processing
 *
 * Processes ModbusTCP IoT proxy messages (send/receive/status) transmitted from backend to frontend via HyperAMP:
 * - Parses ModbusTCP address (IotAddr.modbustcp_addr) from IoT protocol data
 * - Validates ModbusTCP-specific parameters (unit_id/ip/port/function_code)
 * - Dispatches to modbustcp_send_to_remote/modbustcp_recv_from_remote based on opcode
 * - Constructs response message (if opcode = IOT_OPCODE_RESPONSE)
 *
 * @param frontend_sess_id Frontend session ID (from ProxyMsgHeader)
 * @param backend_sess_id Backend IoT session ID (from ProxyMsgHeader)
 * @param iot_header Pointer to parsed IotMsgHeader
 * @param iot_data Pointer to ModbusTCP protocol data (IotAddr + payload)
 * @return int Processing result
 *         - FRONTEND_PROXY_PROCESS_OK: ModbusTCP message processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed (invalid ModbusTCP addr/params/session)
 *
 * @note Internal sub-function - called only by frontend_proxy_iot_msg_process()
 * @note Handles backend-to-frontend ModbusTCP messages sent via HyperAMP
 * @note Binds to IoTFrontendSession via frontend_sess_id before processing
 * @note Automatically updates ModbusTCP session statistics (tx/rx packets/bytes)
 */
int frontend_proxy_modbustcp_msg_process(uint16_t frontend_sess_id,
                                        uint16_t backend_sess_id,
                                        IotMsgHeader *iot_header,
                                        uint8_t *iot_data){
    utils_print("In %s\n", __func__);
    IoTFrontendSession *iot_sess;
    IotModbusTcpAddr   *modbus_tcp_addr;
    uint8_t            *iot_payload;
    IotMsgBuffer       iot_msg_buf;
    int                ret;

    if(NULL == iot_header || NULL == iot_data){
        error_print("frontend_proxy_modbustcp_msg_process failed: invalid parameters!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("iot_header->payload_len = %d\n", iot_header->payload_len);

    if(iot_header->payload_len <= sizeof(IotModbusTcpAddr)){
        error_print("frontend_proxy_modbustcp_msg_process failed: invalid iot_header->payload_len!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == frontend_modbustcp_sess){
        error_print("frontend_proxy_modbustcp_msg_process failed: ModbusTcp session does not initialize!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    iot_sess = frontend_modbustcp_sess;
    (void)iot_sess;

    modbus_tcp_addr         = (IotModbusTcpAddr *)iot_data;
    iot_payload             = iot_data + sizeof(IotModbusTcpAddr);

    (void)modbus_tcp_addr;
    (void)iot_payload;

    iot_msg_buf.addr.addr_type = IOT_PROTO_TYPE_MODBUSTCP;
    iot_msg_buf.data           = iot_payload;
    iot_msg_buf.len            = iot_header->payload_len - sizeof(sizeof(IotModbusTcpAddr));
    memcpy(&iot_msg_buf.addr.addr_info.modbus_tcp_addr, modbus_tcp_addr, sizeof(IotModbusTcpAddr));

    ret = iot_sess->recv_from_backend(iot_sess, &iot_msg_buf, iot_payload, iot_header->payload_len - sizeof(sizeof(IotModbusTcpAddr)));

    return ret;
}


/**
 * @brief Helper function to calculate protocol-specific IoT address length (NEW)
 * 
 * Calculates the actual byte length of valid address data for a given IoT protocol type (avoids padding):
 * - BLUETOOTH: 8 bytes (6-byte MAC + 2-byte port)
 * - CAN: 7 bytes (2-byte port + 4-byte CAN ID + 1-byte bus ID)
 * - ZIGBEE: 11 bytes (8-byte MAC + 2-byte PAN ID + 1-byte endpoint)
 * - LORA: 11 bytes (8-byte DevEUI + 2-byte port + 1-byte freq band)
 * - POWERLINK: 9 bytes (2-byte NodeID + 6-byte MAC + 2-byte PDO ID)
 * - UNKNOWN: 0 bytes (invalid)
 * 
 * @param addr_type IoT protocol type (from IotProtoType)
 * @return size_t Valid address length (bytes) for the protocol; 0 if invalid
 * 
 * @note Used by build_proxy_iot_message to validate header->iot_addr_len
 * @note Ensures minimal address data is included in the message (no unused union space)
 * @note Returns 0 for all unrecognized/invalid IotProtoType values (fast fail)
 */
size_t get_iot_addr_length(IotProtoType addr_type)
{
    // Return exact address length per protocol type (no padding)
    switch (addr_type) {
        case IOT_PROTO_TYPE_BLUETOOTH:
            return sizeof(IotBtAddr); 
        case IOT_PROTO_TYPE_CAN:
            return sizeof(IotCanAddr);
        case IOT_PROTO_TYPE_ZIGBEE:
            return sizeof(IotZigbeeAddr);
        case IOT_PROTO_TYPE_LORA:
            return sizeof(IotLoraAddr);
        case IOT_PROTO_TYPE_POWERLINK:
            return sizeof(IotPowerLinkAddr); 
        case IOT_PROTO_TYPE_MODBUSTCP:
            return sizeof(IotModbusTcpAddr); 
        default:
            // Invalid/unknown protocol type - return 0 (invalid length)
            return 0;
    }
}