#include "frontend_api.h"



/**
 * @brief Create and initialize a new FrontendSession instance
 *
 * This function is responsible for creating a FrontendSession object by first validating the input
 * FrontendEngine_ context (including its session pool and associated operations), then allocating
 * memory for the new session, and initializing the core association between the session and the engine.
 * The new session will hold a reference to the input engine, while other session-specific fields
 * remain uninitialized and require further setup by the caller.
 *
 * @param[in] eng Pointer to a FrontendEngine_ instance that provides the necessary context for session creation,
 *                including a valid session pool (`sess_pool`) and corresponding operation set (`sess_pool->ops`).
 *                Must not be NULL, and its nested `sess_pool` and `sess_pool->ops` must also be non-NULL.
 *
 * @return struct FrontendSession* 
 *         - Pointer to the newly created and partially initialized FrontendSession instance on success.
 *         - NULL on failure (potential causes: invalid engine context (eng/sess_pool/ops is NULL),
 *           or memory allocation for FrontendSession fails).
 *
 * @note 1. This function only initializes the `eng` field of the FrontendSession; other member fields
 *          (e.g., session ID, connection status, protocol parameters) remain uninitialized and need
 *          to be set explicitly by the caller before using the session.
 *       2. The memory of the returned FrontendSession is allocated via `malloc`. The caller is responsible
 *          for managing its lifecycle (e.g., releasing memory via `free` when the session is no longer needed)
 *          to avoid memory leaks.
 *       3. The validity of `eng->sess_pool` and `eng->sess_pool->ops` is a prerequisite for subsequent
 *          session management operations (e.g., connection, release), hence the strict validation here.
 *
 * @warning 1. If `eng` is NULL, or `eng->sess_pool` is NULL, or `eng->sess_pool->ops` is NULL, the function
 *             will immediately return NULL and print an error message ("invalid engine context"), without
 *             attempting memory allocation.
 *          2. Memory allocation failure (returned by `malloc`) will also result in a NULL return and an
 *             error message. The caller must check the return value before using the session pointer to
 *             prevent null pointer dereference crashes.
 *          3. Uninitialized fields of the returned FrontendSession may contain garbage values; do not use
 *             the session for connection or other operations until all required fields are properly initialized.
 */
struct FrontendSession *frontend_sess_new(struct FrontendEngine_ *eng){
    struct FrontendSession *new_sess;

    if(NULL == eng || NULL == eng->sess_pool || NULL == eng->sess_pool->ops){
        error_print("frontend_sess_new failed: invalid engine context!\n");
        return NULL;
    }

    new_sess = malloc(sizeof(struct FrontendSession));

    if(NULL == new_sess){
        error_print("frontend_sess_new failed: failed to allocate memory for FrontendSession!\n");
        return NULL;      
    }

    new_sess->eng               = eng;
    new_sess->event_callback    = default_session_event_callback;

    return new_sess;
}

/**
 * @brief Build a frontend session connection request based on the IP:PORT string and specified protocol, 
 *        then send it to the shared memory queue
 *
 * This function accepts a frontend session object, protocol type, and an IP:PORT formatted address string.
 * It will attempt to convert the proto and IP:PORT into an instance of struct SessMsgPara. If the conversion
 * is successful, it will call the frontend_sess_connect function. Additionally, it parses the address information,
 * constructs a connection request, and finally sends the request to the shared memory queue to complete the
 * initialization and delivery of the connection request.
 *
 * @param[in,out] sess Pointer to the frontend session object, used to store session-related configurations 
 *                     and status information. Valid memory must be pre-allocated (non-NULL).
 * @param[in]     proto Connection protocol type (valid values: predefined protocol macros corresponding 
 *                      to TCP/UDP, which must be semantically consistent with the protocol field in SessMsgPara).
 * @param[in]     addr_str IP:PORT formatted address string (supports IPv4: "x.x.x.x:port" or IPv6: "[ipv6_addr]:port".
 *                         Cannot be NULL or an empty string).
 *
 * @return int Function execution result:
 *             - FRONTEND_PROXY_PROCESS_OK: Successfully converted to struct SessMsgPara, called frontend_sess_connect,
 *                                           and sent the connection request to the shared memory queue.
 *             - FRONTEND_PROXY_PROCESS_ERROR: Execution failed (potential causes: sess is NULL, invalid format of addr_str,
 *                                             invalid protocol type, failed conversion to struct SessMsgPara,
 *                                             failed call to frontend_sess_connect, failed operation on the shared memory queue, etc.).
 *
 * @note 1. addr_str must strictly comply with the "IP:PORT" format.
 *       2. The proto parameter must be a predefined valid protocol macro (e.g., SESS_TCP_PROTO/SESS_UDP_PROTO). Passing an invalid
 *          value will result in conversion failure and return FRONTEND_PROXY_PROCESS_ERROR.
 *       3. The sess pointer must point to effectively allocated memory. The function will not actively allocate or free
 *          the sess memory; the caller is responsible for its lifetime management.
 *       4. The initialization status of the shared memory queue is guaranteed by the underlying module. If the queue is
 *          not ready, the function will return FRONTEND_PROXY_PROCESS_ERROR.
 *
 * @warning If sess is NULL, addr_str is NULL, or addr_str has an invalid format, the function will immediately return
 *          FRONTEND_PROXY_PROCESS_ERROR without performing the conversion to struct SessMsgPara, calling frontend_sess_connect,
 *          or executing the connection request construction and sending operations.
 */
int frontend_sess_connect_by_addrstr(struct FrontendSession *sess, int proto, const char *addr_str){
    struct SessMsgPara      para;
/*
 * When the frontend session ID is set to FRONTEND_HANDOVER_SESSION_ID, the frontend protocol will allocate
 * a frontend session ID before sending the SESSION-CREATION command to the backend proxy.
 */
    para.frontend_sess_id   = FRONTEND_HANDOVER_SESSION_ID;
    para.backend_sess_id    = BACKEND_HANDOVER_SESSION_ID;
    para.ip_version         = SESS_IPV4_PROTO;
    para.dev_id             = DEV_ID_AUTO_HANDOVER;
    para.trans_proto        = proto;
    IPV4_PORT_STR_TO_TUPLE(addr_str, para.ip_port_tuple.ipv4_port_tuple);

    utils_print("In %s:\n", __func__);
    utils_print("frontend_sess_id = %d, backend_sess_id = %d, ip_version = %d, dev_id = %d, trans_proto = %d\n",
         para.frontend_sess_id, para.backend_sess_id, para.ip_version, para.dev_id, para.trans_proto);

    return frontend_sess_connect(sess, &para);
}


/**
 * @brief Build a frontend session connection request based on the IP:PORT string, specified protocol and device ID,
 *        then send it to the shared memory queue
 *
 * This function accepts a frontend session object, protocol type, an IP:PORT formatted address string and a device ID.
 * It will attempt to convert the proto, dev_id and IP:PORT into an instance of struct SessMsgPara. If the conversion
 * is successful, it will call the frontend_sess_connect function. Additionally, it parses the address information,
 * constructs a connection request, and finally sends the request to the shared memory queue to complete the
 * initialization and delivery of the connection request. Compared with frontend_sess_connect_by_addrstr, this function
 * supports specifying a custom device ID instead of using the automatic handover device ID.
 *
 * @param[in,out] sess Pointer to the frontend session object, used to store session-related configurations
 *                     and status information. Valid memory must be pre-allocated (non-NULL).
 * @param[in]     proto Connection protocol type (valid values: predefined protocol macros corresponding
 *                      to TCP/UDP, which must be semantically consistent with the protocol field in SessMsgPara).
 * @param[in]     addr_str IP:PORT formatted address string (supports IPv4: "x.x.x.x:port" or IPv6: "[ipv6_addr]:port".
 *                         Cannot be NULL or an empty string).
 * @param[in]     dev_id Custom device ID used to specify the target device for the session connection,
 *                       replaces the default DEV_ID_AUTO_HANDOVER value in the connection parameter structure.
 *
 * @return int Function execution result:
 *             - FRONTEND_PROXY_PROCESS_OK: Successfully converted to struct SessMsgPara, called frontend_sess_connect,
 *                                           and sent the connection request to the shared memory queue.
 *             - FRONTEND_PROXY_PROCESS_ERROR: Execution failed (potential causes: sess is NULL, invalid format of addr_str,
 *                                             invalid protocol type, invalid dev_id value, failed conversion to struct SessMsgPara,
 *                                             failed call to frontend_sess_connect, failed operation on the shared memory queue, etc.).
 *
 * @note 1. addr_str must strictly comply with the "IP:PORT" format.
 *       2. The proto parameter must be a predefined valid protocol macro (e.g., SESS_TCP_PROTO/SESS_UDP_PROTO). Passing an invalid
 *          value will result in conversion failure and return FRONTEND_PROXY_PROCESS_ERROR.
 *       3. The sess pointer must point to effectively allocated memory. The function will not actively allocate or free
 *          the sess memory; the caller is responsible for its lifetime management.
 *       4. The initialization status of the shared memory queue is guaranteed by the underlying module. If the queue is
 *          not ready, the function will return FRONTEND_PROXY_PROCESS_ERROR.
 *       5. The dev_id parameter will be directly assigned to the dev_id field of struct SessMsgPara, overriding the default
 *          DEV_ID_AUTO_HANDOVER configuration; ensure the passed device ID is valid and available.
 *
 * @warning If sess is NULL, addr_str is NULL, addr_str has an invalid format, or dev_id is invalid, the function will immediately return
 *          FRONTEND_PROXY_PROCESS_ERROR without performing the conversion to struct SessMsgPara, calling frontend_sess_connect,
 *          or executing the connection request construction and sending operations.
 */
int frontend_sess_connect_by_addrstr_devid(struct FrontendSession *sess, int proto, const char *addr_str, uint16_t dev_id){
        struct SessMsgPara      para;
/*
 * When the frontend session ID is set to FRONTEND_HANDOVER_SESSION_ID, the frontend protocol will allocate
 * a frontend session ID before sending the SESSION-CREATION command to the backend proxy.
 */
    para.frontend_sess_id   = FRONTEND_HANDOVER_SESSION_ID;
    para.backend_sess_id    = BACKEND_HANDOVER_SESSION_ID;
    para.ip_version         = SESS_IPV4_PROTO;
    para.dev_id             = dev_id;          // Use the incoming custom device ID
    para.trans_proto        = proto;
    IPV4_PORT_STR_TO_TUPLE(addr_str, para.ip_port_tuple.ipv4_port_tuple);

    utils_print("In %s:\n", __func__);
    utils_print("frontend_sess_id = %d, backend_sess_id = %d, ip_version = %d, dev_id = %u, trans_proto = %d\n",
         para.frontend_sess_id, para.backend_sess_id, para.ip_version, para.dev_id, para.trans_proto);

    return frontend_sess_connect(sess, &para);
}


/**
 * @brief Build and send a frontend session connection request based on the provided SessMsgPara configuration
 *
 * This function uses the protocol, IP address, and port information encapsulated in struct SessMsgPara,
 * combines it with the frontend session object (struct FrontendSession), to construct a valid connection request.
 * It verifies the validity of input parameters first, then delivers the constructed request to the shared memory queue,
 * completing the core process of frontend session connection initialization.
 *
 * @param[in,out] sess Pointer to the frontend session object, used to store session-related configurations,
 *                     runtime status, and connection context. Must point to pre-allocated valid memory (non-NULL).
 * @param[in]     para Pointer to the SessMsgPara structure that contains connection core parameters:
 *                     - Protocol type (TCP/UDP, consistent with predefined protocol macros)
 *                     - Target IP address (valid IPv4/IPv6 string)
 *                     - Target port number (valid range: 1~65535)
 *                     Must be non-NULL and contain legally valid configuration data.
 *
 * @return int Function execution result:
 *             - FRONTEND_PROXY_PROCESS_OK: Successfully verified parameters, constructed the connection request,
 *                                           and sent it to the shared memory queue.
 *             - FRONTEND_PROXY_PROCESS_ERROR: Execution failed (potential causes: sess/para is NULL, invalid protocol
 *                                             type in para, illegal IP address/port in para, failed shared memory queue
 *                                             operation, or invalid session state in sess, etc.).
 *
 * @note 1. This function is the core implementation of frontend session connection; frontend_sess_connect_by_addrstr()
 *          calls this function after converting "IP:PORT" string and protocol to a valid struct SessMsgPara instance.
 *       2. The function does not parse or correct the parameters in para (e.g., IP format, port range); the caller
 *          must ensure para contains legally valid configuration before calling.
 *       3. The sess object must be properly initialized (e.g., default state set) before calling; uninitialized
 *          sess may lead to unexpected behavior or failure.
 *       4. The shared memory queue's availability is guaranteed by the underlying module; if the queue is uninitialized
 *          or full, the function will return FRONTEND_PROXY_PROCESS_ERROR directly.
 *
 * @warning 1. If sess or para is NULL, the function will immediately return FRONTEND_PROXY_PROCESS_ERROR without
 *             performing any connection request construction or delivery operations.
 *          2. Passing para with illegal parameters (e.g., invalid protocol, out-of-range port, malformed IP) will
 *             result in request construction failure and return FRONTEND_PROXY_PROCESS_ERROR.
 */
int frontend_sess_connect(struct FrontendSession *sess, struct SessMsgPara *para){
    struct FrontendEngine_              *eng;
    struct FrontendSessionPool          *sess_pool;
    struct FrontendSessionPoolOps       *ops;
    int                                 ret;

    eng         = sess->eng;
    sess_pool   = eng->sess_pool;
    ops         = sess_pool->ops;

    if(NULL == ops->create_sess_step1){
        error_print("frontend_sess_connect failed: sess_pool->ops->create_sess_step1 is NULL (required session creation step 1 function not configured)!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    utils_print("In %s, addr is %d.%d.%d.%d port = %d\n", __func__, 
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[0],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[1],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[2],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[3],  
                para->ip_port_tuple.ipv4_port_tuple.port);
    
    utils_print("frontend_sess_id = %d, backend_sess_id = %d, ip_version = %d, dev_id = %d, trans_proto = %d\n",
                para->frontend_sess_id, para->backend_sess_id, para->ip_version, para->dev_id, para->trans_proto);

    ret = ops->create_sess_step1(sess_pool, sess, para);

    return ret;
}


/**
 * @brief Calculate the number of message segments required for sending a message (includes header + data)
 * 
 * Each segment can hold up to `bsize` bytes (fixed header + message data). 
 * Uses safe ceil division to ensure full data coverage without integer overflow.
 * 
 * @param dsize Total size of message data to send (bytes, non-negative)
 * @param bsize Max capacity of a single message segment (bytes, must be > hsize)
 * @param hsize Fixed size of the segment header (bytes, non-negative)
 * 
 * @return size_t Required segment count:
 *         - 0: Invalid param (bsize <= hsize) OR No data to send (dsize == 0)
 *         - >=1: Valid count (all data + headers fit in segments)
 * 
 * @note 1. Effective data per segment: effective = bsize - hsize.
 *       2. Safe Ceil Division: Uses formula ((dsize - 1) / effective) + 1 to prevent 
 *          overflow that could occur with (dsize + effective - 1).
 *       3. Edge case handling: 
 *          - If dsize == 0, returns 0 (no segments needed for empty payload).
 *          - If bsize <= hsize, returns 0 immediately as no data can be transmitted.
 *       4. Type Safety: All intermediate calculations are performed in size_t (unsigned).
 */
#define CALC_MSG_SEG_NUM(dsize, bsize, hsize) \
( \
    /* Check validity: segment capacity must be strictly greater than header size */ \
    ((bsize) <= (hsize)) ? 0U : \
    ( \
        /* Edge case: Empty message requires 0 segments */ \
        ((dsize) == 0U) ? 0U : \
        /* Safe ceiling division: (dsize - 1) / effective_capacity + 1 */ \
        /* This avoids overflow risks associated with (dsize + effective_capacity - 1) */ \
        (((dsize) - 1U) / ((bsize) - (hsize)) + 1U) \
    ) \
)



#if 0
/**
 * @brief Write data to the send buffer of a frontend session (for frontend session data transmission)
 *
 * This function is used to write specified data into the send buffer of a frontend session (FrontendSession).
 * The underlying communication mechanism will subsequently be responsible for sending the buffered data to the frontend.
 * Data is not sent directly to the network immediately, but first written to the buffer, which is limited by the remaining buffer space.
 *
 * @param[in] sess Pointer to the frontend session instance, pointing to the frontend session object to operate on; must not be NULL
 * @param[in] data Pointer to the buffer of data to be sent, pointing to the start address of the data to be written into the send buffer; must not be NULL
 * @param[in] size Length of the data to be sent (unit: bytes); must be greater than 0, otherwise no valid data can be written
 *
 * @return int Function execution result with the following specific meanings:
 *         - >0: Number of bytes successfully written to the send buffer (may be less than the requested size, depending on the remaining buffer space)
 *         - =0: Insufficient send buffer space, no data could be written
 *         - -1: Send error (e.g., invalid session, invalid parameters, failed underlying buffer operation, etc.)
 *
 * @note 1. Both parameters sess and data must be valid non-NULL pointers; otherwise, -1 may be returned and undefined behavior may be triggered;
 *       2. If the return value is greater than 0 but less than size, it indicates that only part of the data was written to the buffer. The remaining data needs to be retried after the buffer has free space;
 *       3. If 0 is returned, it is recommended to retry the call after a short delay; if -1 is returned, the session status and parameter validity should be checked first.
 */
int frontend_sess_send(struct FrontendSession *sess, uint8_t *data, uint32_t size){
    int block_size, seg_num, data_size, header_size, eff_data_per_seg, snd_size, cur_size, ret;
    struct SessMsgSeg       *msg_seg;
    GeneralProxyMsgHeader   data_msg_hdr;
    uint8_t                 *payload, **res_pointer;
    uint8_t                 *res_buf[100] = {NULL};

    block_size = SESS_GET_HYPER_QUEUE_BLOCK_SIZE(sess, tx);

    if(0 == block_size){
        error_print("frontend_sess_send failed: the TX queue is not initialized correctly!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    data_size           = size;
    header_size         = PROXY_MSG_HDR_SIZE;
    seg_num             = CALC_MSG_SEG_NUM(data_size, block_size, header_size);
    eff_data_per_seg    = block_size - header_size;

#if 0
    memset(&data_msg_hdr, 0, sizeof(data_msg_hdr));

    data_msg_hdr.outer_header.version           = PROXY_PROTO_VERSION_1;
    data_msg_hdr.outer_header.proxy_msg_type    = PROXY_MSG_TYPE_DATA;
    data_msg_hdr.outer_header.backend_sess_id   = sess->backend_sess_id;
    data_msg_hdr.outer_header.frontend_sess_id  = sess->frontend_sess_id;
#endif
/*
 * Copy data into message segments. Each message segment carries a PROXY-DATA message whose payload does not exceed block_size - header_size.
 */
    snd_size        = 0;
    payload         = data;
    res_pointer     = res_buf;

    while(seg_num > 0){
        if(data_size  > eff_data_per_seg){
            cur_size = eff_data_per_seg;
        }else{
            cur_size = data_size;
        }

//        msg_seg = sess_msg_seg_alloc_lite(SESS_MSG_SEG_DYNAMIC_ALLOC);
        msg_seg = sess_msg_seg_alloc(cur_size, SESS_MSG_SEG_DYNAMIC_ALLOC, NULL, NULL);

        if(NULL == msg_seg){
            error_print("frontend_sess_send failed: insufficient memory for allocating message segment instance!\n");
            goto seg_frag_imcomplete;
        }
#if 0
        data_msg_hdr.outer_header.payload_len = cur_size;
        ret =  build_proxy_general_message(sess->eng, &data_msg_hdr, payload, cur_size, res_pointer, MEMORY_ALLOC_CALLER, NULL);

        if(FRONTEND_PROXY_PROCESS_ERROR == ret){
            error_print("frontend_sess_send failed: build data message error!\n");
            goto seg_frag_error; 
        }

        msg_seg->data   = res_pointer;
        msg_seg->len    = cur_size + sizeof(ProxyMsgHeader);
#endif
        memcpy(msg_seg->data, payload, cur_size);

        SESS_MSG_SEG_INSERT_QUEUE(sess, msg_seg, f2b);

        snd_size    += cur_size;
        payload     += cur_size;
        data_size   -= cur_size;

        seg_num--;
    }// while
/*
 * All data is orgnized in frontend-to-backend message queue (msg_f2b).
 */
    FRONTEND_SESS_LINK_TO_QUEUE(sess, f2b);
    return snd_size;

seg_frag_imcomplete:

/*
 * snd_size > 0 means at least one message segment has been inserted into the session's frontend-to-backend message queue (msg_f2b). This session should be marked as an active session (link to
 * queue_f2b), so the frontend protocol module will process it and send the data in msg_f2b via the shared memory TX queue.
 */
    if(snd_size > 0){
        FRONTEND_SESS_LINK_TO_QUEUE(sess, f2b);
    }

    return snd_size;

seg_frag_error:
    FRONTEND_SESS_UNLINK_FROM_QUEUE(sess, f2b);
    return -1;
}
#endif


/**
 * @brief Fragment user data and enqueue pure payloads into the frontend-to-backend queue (msg_f2b).
 *
 * This function splits input data into chunks, reserving space for protocol headers 
 * (which will be added by the subsequent sending logic).
 * 
 * Key Features:
 * 1. Robust Looping: Driven by remaining data size, not pre-calculated segment counts.
 * 2. Partial Success: If memory allocation fails mid-stream, already enqueued segments 
 *    are kept and marked for sending. Returns the number of bytes successfully enqueued.
 * 3. No Dead Code: Removed unreachable error handling paths.
 *
 * @param[in] sess Pointer to the frontend session instance.
 * @param[in] data Pointer to the user payload data.
 * @param[in] size Total length of the user payload data.
 *
 * @return int 
 *         - >0: Number of bytes successfully enqueued.
 *         - 0:  No data enqueued (e.g., first allocation failed).
 *         - -1: Critical parameter error.
 */
int frontend_sess_send(struct FrontendSession *sess, uint8_t *data, uint32_t size) {
    struct SessMsgSeg *msg_seg;
    uint32_t          block_size;
    uint32_t          header_reserved_size;
    uint32_t          max_payload_per_seg;
    uint32_t          total_enqueued;
    uint32_t          cur_chunk_size;
    uint32_t          remaining_data;
    uint8_t           *src_ptr;

    utils_print("In %s\n, preparing to send data:", __func__);
    // DUMP_BUFFER_CONTENT(data, size, "%c"); /* 严重性能问题：切勿在发大文件时开启全量十六进制/字符打印！*/
    /* 1. Parameter Validation */
    if (NULL == sess || NULL == data || 0 == size) {
        error_print("frontend_sess_send failed: invalid parameters!\n");
        return -1; /* Use -1 for invalid args */
    }

    /* 2. Get Queue Block Size */
    block_size = SESS_GET_HYPER_QUEUE_BLOCK_SIZE(sess, tx);
    if (0 == block_size) {
        error_print("frontend_sess_send failed: TX queue not initialized!\n");
        return -1;
    }


    /* Calculate limits */
    header_reserved_size  = PROXY_MSG_HDR_SIZE;
    
    if (block_size <= header_reserved_size) {
        error_print("frontend_sess_send failed: Block size too small for header reservation!\n");
        return -1;
    }
    
    max_payload_per_seg = block_size - header_reserved_size;

    utils_print("data size = %d, block_size = %d, header_reserved_size = %d, max_payload_per_seg = %d\n", size, block_size, header_reserved_size, max_payload_per_seg);

    total_enqueued = 0;
    remaining_data = size;
    src_ptr        = data;

    /* 3. Fragmentation Loop (Driven by remaining data, NOT pre-calculated seg_num) */
    while (remaining_data > 0) {
        /* Determine chunk size: min(remaining, max_per_seg) */
        cur_chunk_size = (remaining_data > max_payload_per_seg) ? max_payload_per_seg : remaining_data;

        /* Allocate Segment (Pure Payload only) */
        msg_seg = sess_msg_seg_alloc(cur_chunk_size, SESS_MSG_SEG_DYNAMIC_ALLOC, NULL, NULL);

        if (NULL == msg_seg) {
            error_print("frontend_sess_send failed: Memory allocation failed!\n");
            /* 
             * PARTIAL SUCCESS: 
             * Stop processing. Already enqueued segments are valid. 
             * Do NOT free them. Do NOT unlink. Just return what we have.
             */
            goto done_partial;
        }

        /* Copy Pure Payload */
        memcpy(msg_seg->data, src_ptr, cur_chunk_size);

        /* Set Metadata */
        msg_seg->len      = cur_chunk_size;
        msg_seg->offset   = 0;
//        msg_seg->type     = SESS_MSG_SEG_DYNAMIC_ALLOC;
        msg_seg->mem_pool = NULL;

        /* Enqueue */
        SESS_MSG_SEG_INSERT_QUEUE(sess, msg_seg, f2b);

        /* Update Counters */
        total_enqueued += cur_chunk_size;
        remaining_data -= cur_chunk_size;
        src_ptr        += cur_chunk_size;
        utils_print("After loop, remaining_data = %d\n", remaining_data);
    }

done_partial:
    /* 4. Finalize: Link Session ONLY if we successfully enqueued something */
    utils_print("[STATE-TAG-1] f2b_queue: state_f2b = %d, total_enqueued = %d\n", sess->state_f2b, total_enqueued);
    if (total_enqueued > 0) {
        FRONTEND_SESS_LINK_TO_QUEUE(sess, f2b);
    }
    utils_print("[STATE-TAG-2] f2b_queue: state_f2b = %d\n", sess->state_f2b);

    return (int)total_enqueued;
}


#if 0
/**
 * @brief Read data from the receive buffer of a frontend session (for frontend session data reception)
 * This function is used to read specified data from the receive buffer of a frontend session (FrontendSession).
 * The underlying communication mechanism has already received data from the frontend and stored it in the receive buffer.
 * Data is not read directly from the network immediately, but from the pre-received buffer, which is limited by the amount of available data in the buffer.
 * The function is non-blocking: it will not block the calling thread waiting for data arrival or buffer operations, and will return immediately regardless of whether valid data is read.
 * If the available space in the receive buffer is smaller than the size of a complete message block, the message block will be truncated, and only the part that fits in the buffer will 
 * be read.
 * 
 * @param[in] sess Pointer to the frontend session instance, pointing to the frontend session object to operate on; must not be NULL
 * @param[inout] data Pointer to the buffer for storing received data, pointing to the start address where the read data will be stored; must not be NULL
 * @param[in] size Maximum length of data that can be stored in the receive buffer (unit: bytes); must be greater than 0, otherwise no valid data can be read
 * @return int Function execution result with the following specific meanings:
 * 0: Number of bytes successfully read from the receive buffer (may be less than the requested size, depending on the available data in the buffer)
 * =0: No available data in the receive queue, no data could be read
 * -1: Receive error (e.g., invalid session, invalid parameters, failed underlying buffer operation, etc.)
 * @note 1. Both parameters sess and data must be valid non-NULL pointers; otherwise, -1 may be returned and undefined behavior may be triggered;
 * If the return value is greater than 0 but less than size, it indicates that only part of the available data was read from the buffer. The remaining data can be retried after new data is 
 * received into the buffer;If 0 is returned, it is recommended to retry the call after a short delay; if -1 is returned, the session status and parameter validity should be checked first.
 */
int frontend_sess_recv(struct FrontendSession *sess, uint8_t *data, uint32_t size){
    struct SessMsgSeg               *cur_seg;
    int                             read_size, ret;

    if(NULL == sess || NULL == data || 0 == size){
        error_print("frontend_sess_recv failed: invalid parameters!\n");
        return -1;
    }

    cur_seg = TAILQ_FIRST(&sess->msg_b2f);
/*
 * The receive queue is empty.
 */
    if(NULL == cur_seg){
        return 0;
    }

    read_size = (size > cur_seg->len) ? cur_seg->len : size;

    memcpy(data, cur_seg->data, read_size);
    TAILQ_REMOVE(&sess->msg_b2f, cur_seg, entry);
    free(cur_seg);

#if 0
/*
 * queue_b2f: Session queue for sessions with pending data to be received by the frontend from the backend.
 * (Pending data refers to data sent from the backend to the frontend, which the frontend needs to read.)
 * If the receive queue (msg_b2f) is empty, the session instance should be removed from the queue_b2f list.
 */
    if(TAILQ_EMPTY(&sess->msg_b2f)){
        FRONTEND_SESS_UNLINK_FROM_QUEUE(sess, b2f);
    }
#endif
    return read_size;
}
#endif


/**
 * @brief Read data from the receive buffer of a frontend session (supports multi-block aggregation)
 * 
 * This function reads data from the receive buffer (FrontendSession) into the user-provided buffer.
 * It aggregates multiple small data blocks (SessMsgSeg) from the TAILQ into a single contiguous 
 * memory copy, as long as the requested 'size' allows.
 * 
 * Key Behaviors:
 * 1. Aggregation: Continuously reads from the queue head until the user buffer is full or queue is empty.
 * 2. Partial Read Handling (Zero Data Loss):
 *    - If a segment fits entirely: It is copied, removed from the queue, and freed.
 *    - If a segment is larger than remaining space: Only the fitting portion is copied.
 *      The segment is KEPT in the queue with updated metadata:
 *      - 'data' pointer is advanced.
 *      - 'len' is decreased.
 *      - 'offset' is increased (to track progress from original base).
 * 3. Non-blocking: Returns immediately based on current availability.
 * 
 * @param[in] sess Pointer to the frontend session instance; must not be NULL.
 * @param[inout] data Pointer to the destination buffer; must not be NULL.
 * @param[in] size Maximum bytes to read; must be > 0.
 * 
 * @return int 
 *         - >0: Total number of bytes successfully read.
 *         - 0:  No data available in the queue.
 *         - -1: Error (invalid parameters).
 * 
 * @note 
 *       - Thread Safety: Assumes exclusive access to sess->msg_b2f. External locking is required if 
 *         accessed by multiple threads.
 *       - Offset Tracking: The 'offset' field in SessMsgSeg is updated to reflect the number of 
 *         bytes consumed from the original allocation.
 */
int frontend_sess_recv(struct FrontendSession *sess, uint8_t *data, uint32_t size) {
    struct SessMsgSeg *cur_seg;
    uint32_t total_read = 0;
    uint32_t copy_len;
    uint8_t *dst_ptr = data;

    /* 1. Parameter Validation */
    if (NULL == sess || NULL == data || 0 == size) {
        error_print("frontend_sess_recv failed: invalid parameters!\n");
        return -1;
    }

    /* 2. Loop to aggregate multiple segments */
    while (total_read < size) {
        cur_seg = TAILQ_FIRST(&sess->msg_b2f);
        
        /* Queue empty: stop reading */
        if (NULL == cur_seg) {
            break;
        }

        /* Calculate remaining space in user buffer */
        uint32_t remaining_space = size - total_read;
        
        if (cur_seg->len <= remaining_space) {
            /* Case A: Segment fits entirely in the remaining buffer */
            copy_len = cur_seg->len;
            
            /* Copy all data from this segment */
            memcpy(dst_ptr, cur_seg->data, copy_len);
            
            /* Update destination pointer and total count */
            dst_ptr += copy_len;
            total_read += copy_len;
            
            /* Remove and free the fully consumed segment */
            TAILQ_REMOVE(&sess->msg_b2f, cur_seg, entry);
            
            /* Optional: Clear pointers before free for safety/debugging */
            cur_seg->data = NULL;
            cur_seg->mem_pool = NULL; 
            
/**
 * Check if the current session message segment is marked as dynamically allocated
 * (SESS_MSG_SEG_DYNAMIC_ALLOC indicates the segment's data buffer was allocated via malloc/calloc/realloc).
 * If true, release the associated memory to avoid memory leaks – this is critical for 
 * maintaining memory integrity in session message processing.
 */            if(SESS_MSG_SEG_DYNAMIC_ALLOC == cur_seg->type){
                free(cur_seg->data);
            }

            free(cur_seg);
            
            /* Continue to next segment */
        } else {
            /* Case B: Segment is larger than remaining buffer space */
            copy_len = remaining_space;
            
            /* Copy only the fitting part */
            memcpy(dst_ptr, cur_seg->data, copy_len);
            
            /* Update destination pointer and total count */
            dst_ptr += copy_len;
            total_read += copy_len;
            
            /* CRITICAL UPDATE: Adjust segment metadata to preserve remaining data */
            cur_seg->data += copy_len;      /* Advance data pointer */
            cur_seg->len -= copy_len;       /* Decrease remaining length */
            cur_seg->offset += (uint16_t)copy_len; /* Increase offset to track consumption */
            
            /* Stop reading because user buffer is now full */
            break; 
        }
    }

    return (int)total_read;
}


/**
 * @brief Send SESSION-CLOSURE command to backend and release the frontend session
 *
 * This function is responsible for sending a SESSION-CLOSURE command message to the backend,
 * notifying it to close the session associated with the given FrontendSession instance. 
 * After the backend completes the session closure and sends a SESSION-CLOSURE response message,
 * the frontend will release all resources occupied by this session.
 *
 * @param[in] sess Pointer to the FrontendSession instance to be closed; must be a valid non-NULL pointer
 *
 * @return Operation status code:
 *         - FRONTEND_PROXY_PROCESS_OK: SESSION-CLOSURE command sent successfully,
 *                                     and the session has been released after receiving backend response
 *         - FRONTEND_PROXY_PROCESS_ERROR: Operation failed, which may be caused by input parameters
 *                                         that do not meet the requirements or temporary failure to
 *                                         construct the SESSION-CLOSURE command message
 *
 * @note 1. If a NULL pointer or invalid FrontendSession instance is passed, the function will immediately
 *          return FRONTEND_PROXY_PROCESS_ERROR;
 *       2. After the function returns successfully, the memory space pointed to by 'sess' may be freed
 *          internally (depending on specific implementation), and subsequent access to this pointer is prohibited;
 *       3. Ensure that any necessary pre-processing (e.g., data synchronization) is completed before calling
 *          this function to avoid data loss during session closure
 */
int frontend_sess_close(struct FrontendSession *sess){
    struct FrontendEngine_              *eng;
    struct FrontendSessionPool          *sess_pool;
    struct FrontendSessionPoolOps       *ops;
    int                                 ret;

    eng         = sess->eng;
    sess_pool   = eng->sess_pool;
    ops         = sess_pool->ops;

    if(NULL == ops->close_sess_step1){
        error_print("frontend_sess_close failed: sess_pool->ops->close_sess_step1 is NULL (required session creation step 1 function not configured)!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    ret = ops->close_sess_step1(sess_pool, sess);

    return ret;
}


/**
 * @brief Binds an event callback function to a specified frontend session instance.
 *
 * @details This function performs NULL validation on the input parameters,
 *          and assigns the user-provided event callback handler to the callback
 *          member of the FrontendSession structure. If any input pointer is NULL,
 *          an error log will be printed and an error status code will be returned.
 *
 * @param[in] sess          Pointer to the target FrontendSession structure instance,
 *                          cannot be a NULL pointer.
 * @param[in] event_callback Function pointer of the session event callback type,
 *                          used to handle session-related events, cannot be a NULL pointer.
 *
 * @return int Execution status code:
 *         - @ref FRONTEND_PROXY_PROCESS_OK    Operation succeeded, callback bound successfully
 *         - @ref FRONTEND_PROXY_PROCESS_ERROR Operation failed, invalid NULL input parameters
 *
 * @note This function only performs parameter validation and direct member assignment,
 *       without thread synchronization or additional state checking for the session.
 */
int frontend_sess_bind_callback(struct FrontendSession *sess, SESS_EVENT_CALLBACK event_callback){
    if(NULL == sess || NULL == event_callback){
        error_print("frontend_sess_bind_callback failed: input pointer parameters must not be NULL!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    sess->event_callback = event_callback;

    return FRONTEND_PROXY_PROCESS_OK;
}


int frontend_iot_sess_send(IoTFrontendSession *sess, uint8_t *data, uint32_t size){
    int             ret, max_payload_size, data_size;
    IotMsgBuffer    iot_msg_buf;
    if(NULL == sess || NULL == data){
        error_print("frontend_iot_sess_send failed: invalid parameters!\n");
        return -1;
    }

    max_payload_size = FRONTEND_IOCT_SESS_GET_MAX_PAYLOAD(sess);

    if(0 == max_payload_size){
        error_print("frontend_iot_sess_send failed: invalid session type!\n");
        return -1;
    }

    data_size = (size < max_payload_size) ? size : max_payload_size;

    iot_msg_buf.data = data;
    iot_msg_buf.len  = data_size;

    ret = sess->send_to_backend(sess, &iot_msg_buf);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_iot_sess_send failed: sess->send_to_backend failed!\n");
        return -1;
    }

    return data_size;
}