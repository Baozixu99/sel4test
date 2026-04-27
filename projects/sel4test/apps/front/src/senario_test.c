#include "senario_test.h"
#include "frontend_api.h"

/*
 * GeneralProxyMsgHeader  dev_enable_msg_hdr, strgy_query_msg_hdr, sess_create_msg_hdr, data_msg_hdr;
 */
GeneralProxyMsgHeader dev_enable_msg_hdr = {
    .outer_header = {
        .version            = PROXY_PROTO_VERSION_1,                       // Protocol version, fixed to 1 as specified
        .proxy_msg_type     = PROXY_MSG_TYPE_DEV,                          // Proxy message type: Device message (0)
        .frontend_sess_id   = FRONTEND_ADMIN_SESSION_ID,                   // Frontend admin session ID, used to match frontend-backend sessions in frontend proxy
        .backend_sess_id    = BACKEND_ADMIN_SESSION_ID,                    // Backend admin session ID, used to match backend-backend sessions in frontend proxy
        .payload_len        = sizeof(DevMsgHeader) + sizeof(DevMsgMask)    // Payload length, set according to actual payload
    },
    .inner_header.dev_hdr   = {                                            // Use device message inner header
        .version            = PROXY_PROTO_DEV_VERSION_1,                   // Protocol version, fixed to 1
        .msg_type           = DEV_MSG_ENABLE,                              // Message type: Enable (1)
        .msg_id             = 0,                                           // Message ID, used for command-response matching
        .action_type        = ACTION_TYPE_COMMAND,                         // Signaling type: Response (1)
        .payload_len        = sizeof(DevMsgMask)                           // Payload length, set according to actual payload
    }
};


// Strategy set message header
GeneralProxyMsgHeader strgy_set_msg_hdr = {
    .outer_header = {
        .version            = PROXY_PROTO_VERSION_1,                                    // Protocol version, fixed to 1 as specified
        .proxy_msg_type     = PROXY_MSG_TYPE_STRGY,                                     // Proxy message type: Strategy message (1)
        .frontend_sess_id   = FRONTEND_ADMIN_SESSION_ID,                                // Frontend admin session ID, used to match frontend-backend sessions in frontend proxy
        .backend_sess_id    = BACKEND_ADMIN_SESSION_ID,                                 // Backend admin session ID, used to match backend-backend sessions in frontend proxy
        .payload_len        = sizeof(StrgyMsgHeader) + sizeof(StrgyCMDEnableMessage)    // Payload length, set according to actual payload
    },
    .inner_header.strgy_hdr = {                                                         // Use strategy message inner header
        .version            = PROXY_PROTO_STRGY_VERSION_1,                              // Protocol version, fixed to 1
        .msg_type           = STRGY_MSG_SET,                                            // Message type: Set (0)
        .msg_id             = 0,                                                        // Message ID, used for command-response matching
        .action_type        = ACTION_TYPE_COMMAND,                                     // Signaling type: Response (0)
        .payload_len        = sizeof(StrgyCMDEnableMessage)                            // Payload length, set according to actual payload
    }
};


// Session create message header
GeneralProxyMsgHeader sess_create_msg_hdr = {
    .outer_header = {
        .version            = PROXY_PROTO_VERSION_1,                            // Protocol version, fixed to 1 as specified
        .proxy_msg_type     = PROXY_MSG_TYPE_SESS,                              // Proxy message type: Session message (2)
        .frontend_sess_id   = 1,                                                // Frontend admin session ID, used to match frontend-backend sessions in frontend proxy
        .backend_sess_id    = BACKEND_HANDOVER_SESSION_ID,                      // Backend admin session ID, used to match backend-backend sessions in backend proxy 
        .payload_len        = sizeof(SessMsgHeader) + sizeof(SessIPv4Params)    // Payload length, set according to actual payload
    },
    .inner_header.sess_hdr  = {                                                 // Use session message inner header
        .version            = PROXY_PROTO_SESS_VERSION_1,                       // Protocol version, fixed to 1
        .msg_type           = SESS_MSG_CREATE,                                  // Message type: Create (1)
        .action_type        = ACTION_TYPE_COMMAND,                              // Signaling type: RESPONSE (1)
        .ip_version         = SESS_IPV4_PROTO,                                  // IP version: IPv4 (4), can be changed to IPv4 (6) if needed
        .payload_len        = sizeof(SessIPv4Params)                            // Payload length, set according to actual payload
    }
};


// data message header
GeneralProxyMsgHeader proxy_data_msg_hdr = {
    .outer_header = {
        .version            = PROXY_PROTO_VERSION_1,                            // Protocol version, fixed to 1 as specified
        .proxy_msg_type     = PROXY_MSG_TYPE_DATA,                              // Proxy message type: Session message (3)
        .frontend_sess_id   = 1,                                                // Frontend session ID, used to match frontend-backend sessions in frontend proxy
        .backend_sess_id    = 1,                                                // Backend  session ID, used to match backend-backend sessions in frontend proxy 
        .payload_len        = 0                                                 // Payload length, set according to actual payload
    },
};

/**
 * @brief Simulate backend-end service responses, construct proxy messages via build_proxy_general_message, and inject them into the shared memory RX queue of the back-end engine
 * @details Implements the core responsibility of "scenario functions" in the "Backend Protocol Stack Unit Test.doc":
 *          1. Calls the existing build_proxy_general_message to construct complete proxy messages (supports both shared memory and caller-allocated memory modes);
 *          2. Injects the constructed messages into the front-end engine's shared memory RX queue in compliance with FIFO read-write rules (avoids queue overflow or data overwriting);
 *          3. Returns detailed injection results to locate issues like queue initialization exceptions or message construction failures,
 *             and triggers the front-end engine's request processing logic (e.g., engine_run reads from RX queue).
 *
 * @param[in]  engine            Pointer to a FrontendEngine object containing the front-end proxy's global context (e.g., runtime configuration, memory pool handles).
 *                               Required by build_proxy_general_message for message construction (e.g., accessing memory management resources).
 *                               Must not be NULL (consistent with build_proxy_general_message's parameter constraint).
 * @param[in]  msg_header        Pointer to a GeneralProxyMsgHeader structure specifying the header of the injected message (e.g., message type, payload length).
 *                               Must not be NULL (passed to build_proxy_general_message as the "header" parameter) and comply with the protocol specification in the document.
 * @param[in]  msg_payload       Pointer to the const uint8_t buffer containing the message payload (business data like device status, strategy config).
 *                               Can be NULL only if msg_payload_len is 0 (consistent with build_proxy_general_message's payload constraint).
 * @param[in]  msg_payload_len   Length of msg_payload in bytes. Must be non-negative and match msg_header->payload_len (if the header has a payload length field)
 *                               to ensure message consistency (follows build_proxy_general_message's parameter rule).
 * @param[in]  alloc_mode        Memory allocation mode for message construction, using the same MemoryAllocMode enum as build_proxy_general_message:
 *                               - MEMORY_ALLOC_SHARED: Allocates memory in the shared memory FIFO ring buffer (rx_queue acts as ring_buf for build_proxy_general_message);
 *                               - MEMORY_ALLOC_CALLER: Requires the caller to pre-allocate memory (build_proxy_general_message populates the pre-allocated buffer).
 * @param[out] result_msg        Double pointer to receive the address of the constructed message (consistent with build_proxy_general_message's "result_msg" parameter):
 *                               - MEMORY_ALLOC_SHARED: Points to the message in the rx_queue's FIFO ring buffer (no caller deallocation needed);
 *                               - MEMORY_ALLOC_CALLER: Points to the caller's pre-allocated buffer (populated with the complete message).
 *                               Must not be NULL.
 * @param[out] result_desc       Buffer to store detailed injection results (e.g., "Request injected successfully into RX queue", "RX queue full, injection failed").
 *                               Helps locate test startup issues (document's "injection status feedback" requirement). Must not be NULL.
 * @param[in]  desc_len          Maximum length of the result_desc buffer to prevent buffer overflow (ensures safe result storage).
 *
 * @return int                   Status code following the back-end protocol stack's unified specification (consistent with build_proxy_general_message's return type):
 *                               - BACKEND_PROXY_PROCESS_OK: Message constructed successfully and injected into RX queue;
 *                               - BACKEND_PROXY_PROCESS_ERROR: General error (e.g., build_proxy_general_message fails, rx_queue uninitialized);
 *
 * @note 1. Depends on the existing build_proxy_general_message function (must ensure its correctness before using this injection function);
 *       2. Before injection, confirm FrontendEngine is initialized (engine_init returns BACKEND_PROXY_PROCESS_OK) and rx_queue is ready (document's test prerequisite);
 *       3. For MEMORY_ALLOC_SHARED, ensure rx_queue is a valid SharedMemoryPoolQueue (used for FIFO ring buffer operations in build_proxy_general_message);
 *       4. For MEMORY_ALLOC_CALLER, the caller must guarantee the pre-allocated buffer (result_msg) is large enough to hold the complete message (header + payload)
 *          (follows build_proxy_general_message's constraint for this mode);
 *       5. Supports all message types defined in the protocol document by configuring msg_header->message_type.
 */
int scenario_msg_inject_frontend(FrontendEngine *engine,
                                GeneralProxyMsgHeader *msg_header,
                                const uint8_t *msg_payload,
                                size_t msg_payload_len,
                                MemoryAllocMode alloc_mode,
                                uint8_t **result_msg,
                                char *result_desc,
                                size_t desc_len){
    int ret;
    
    // 1. Check if BackendEngine pointer is NULL (engine initialization is a prerequisite per "Backend Protocol Stack Unit Test.doc")
    if (engine == NULL)
    {
        error_print("scenario_msg_inject_frontend failed: BackendEngine pointer is NULL, engine must be initialized!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
#if 0
    // 2. Check if engine->rx_queue (shared memory RX queue) is NULL (queue initialization is required for message injection, per document)
    if (engine->rx_queue == NULL)
    {
        error_print("scenario_msg_inject_frontend failed: engine->rx_queue (SharedMemoryPoolQueue) is NULL, RX queue must be initialized via engine_init_shared_mem_queue");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
#endif

    // 3. Check if GeneralProxyMsgHeader pointer is NULL (valid header is required for message parsing, per document's message handling rules)
    if (msg_header == NULL)
    {
        error_print("scenario_msg_inject_frontend failed: GeneralProxyMsgHeader pointer is NULL, valid message header is required");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // 4. Check if result_msg double pointer is NULL (used to store constructed message address, per document's scenario function requirements)
    if (result_msg == NULL)
    {
        error_print("scenario_msg_inject_frontend failed: result_msg double pointer is NULL, cannot store address of constructed message");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // 5. Check if result_desc pointer is NULL (used to feedback injection status, per document's "injection status feedback" requirement)
    if (result_desc == NULL)
    {
        error_print("scenario_msg_inject_frontend failed: result_desc pointer is NULL, cannot store injection result description");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // 6. Check consistency between msg_payload and msg_payload_len (empty payload requires len=0, per document's data message integrity rules)
    if (msg_payload == NULL && msg_payload_len > 0)
    {
        error_print("scenario_msg_inject_frontend failed: msg_payload is NULL but msg_payload_len > 0, violates payload integrity rules");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // 7. Check if MemoryAllocMode is valid (covers undefined modes, aligns with document's "shared memory/caller-allocated dual mode")
    if (alloc_mode != MEMORY_ALLOC_SHARED && alloc_mode != MEMORY_ALLOC_CALLER && alloc_mode != MEMORY_ALLOC_AMPQUEUE)
    {
        error_print("scenario_msg_inject failed: invalid MemoryAllocMode, only MEMORY_ALLOC_SHARED and MEMORY_ALLOC_CALLER are supported");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    // 8. Check if desc_len is valid (prevents buffer overflow when writing result_desc, per safe coding practices in document context)
    if (desc_len == 0)
    {
        error_print("scenario_msg_inject failed: desc_len is 0, insufficient buffer size for result_desc");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

/* ------------------------------
 * Normal logic starts here (e.g., call build_proxy_general_message, inject to engine->rx_queue)
 *------------------------------
 * Example: Initialize result_desc with success info first (if no errors)
 * strncpy(result_desc, "scenario_msg_inject: request injection started", desc_len - 1);
 * result_desc[desc_len - 1] = '\0';
 * ... (call build_proxy_general_message, handle queue injection, etc.)
 */

    utils_print("In %s, before enter build_proxy_general_message, build message type = %d, msg_payload_len = %d\n", __func__, msg_header->outer_header.proxy_msg_type, msg_payload_len);
    ret = build_proxy_general_message(engine, msg_header, msg_payload, msg_payload_len, result_msg, alloc_mode, engine->rx_queue);

    return ret;
}


/**
 * @brief Inject a device message into the frontend engine
 * @details Handles injection of device-related messages, processing according to
 *          device management logic and returning results via output parameters.
 * 
 * @param engine Pointer to the BackendEngine instance
 * @return int Return code indicating processing result:
 *         - FRONTEND_PROXY_PROCESS_OK: Message injected and processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to inject or process the message
 */
int device_msg_inject_frontend(FrontendEngine *engine){
    GeneralProxyMsgHeader *dev_msg_hdr;
    DevMsgReport           dev_msg_resp;
    int                   ret, desc_len = 100;
    uint8_t               **res_string;
    char                  *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};

    dev_msg_hdr          = &dev_enable_msg_hdr;

    dev_msg_resp.status  = SESS_OP_STATUS_SUCCESS;
    dev_msg_resp.error   = SESS_OP_CODE_SUCCESS;
    dev_msg_resp.data    = 0xFF;


    res_string           = res_buf;
    desc_string          = desc_buf;

    utils_print("In %s, before enter scenario_msg_inject\n", __func__);
    ret = scenario_msg_inject_frontend(engine, dev_msg_hdr, &dev_msg_resp, sizeof(dev_msg_resp), MEMORY_ALLOC_SHARED, res_string, desc_string, desc_len);

    return ret;
}



/**
 * @brief Inject a strategy message into the frpmtend engine
 * @details Handles injection of strategy/policy-related messages, processing according to
 *          strategy management logic and returning results via output parameters.
 * 
 * @param engine Pointer to the BackendEngine instance
 * @return int Return code indicating processing result:
 *         - FRONTEND_PROXY_PROCESS_OK: Message injected and processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to inject or process the message
 */
int strategy_msg_inject_frontend(FrontendEngine *engine){
    GeneralProxyMsgHeader  *strgy_msg_hdr;
    StrgyMsgReport         strgy_resp;
    int                    ret, desc_len = 100;

    uint8_t               **res_string;

    char                  *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};


    strgy_msg_hdr          = &strgy_set_msg_hdr;
    strgy_resp.error       = STRGY_OP_STATUS_SUCCESS;
    strgy_resp.status      = STRGY_OP_CODE_SUCCESS;
    res_string             = res_buf;
    desc_string            = desc_buf;

    ret = scenario_msg_inject_frontend(engine, strgy_msg_hdr, &strgy_resp, sizeof(strgy_resp), MEMORY_ALLOC_SHARED, res_string, desc_string, desc_len);

    return ret;
}


/**
 * @brief Inject a session message into the frontend engine
 * @details Handles injection of session-related messages, processing according to
 *          session management logic and returning results via output parameters.
 * 
 * @param engine Pointer to the BackendEngine instance
 * @return int Return code indicating processing result:
 *         - FRONTEND_PROXY_PROCESS_OK: Message injected and processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to inject or process the message
 */
int session_msg_inject_frontend(FrontendEngine *engine){
    GeneralProxyMsgHeader   *sess_msg_hdr;
    SessOpRespData          sess_msg_resp;
    int                     ret, desc_len = 100;
    uint8_t                 **res_string;
    char                    *desc_string;
    uint8_t                 *res_buf[100] = {NULL};
    char                    desc_buf[100] = {0};

    sess_msg_hdr            = &sess_create_msg_hdr;
    sess_msg_resp.status    = SESS_OP_STATUS_SUCCESS;
    sess_msg_resp.code      = SESS_OP_CODE_SUCCESS;
    res_string              = res_buf;
    desc_string             = desc_buf;

    ret = scenario_msg_inject_frontend(engine, sess_msg_hdr, &sess_msg_resp, sizeof(sess_msg_resp), MEMORY_ALLOC_SHARED, res_string, desc_string, desc_len);

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Inject a data message into the frontend engine
 * @details Handles injection of data/content-related messages, processing according to
 *          data processing logic and returning results via output parameters.
 * 
 * @param engine Pointer to the BackendEngine instance
 * @return int Return code indicating processing result:
 *         - FRONTEND_PROXY_PROCESS_OK: Message injected and processed successfully
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to inject or process the message
 */
int data_msg_inject_frontend(FrontendEngine *engine){
    GeneralProxyMsgHeader *data_msg_hdr;
    uint8_t **res_string;
    char *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};
    char data_buf[100];
    int ret,  desc_len = 100;

    memset(data_buf, 0, sizeof(data_buf));
    snprintf(data_buf, sizeof(data_buf), "test msg");

    utils_print("strlen(test msg) = %d\n", strlen("test msg"));
    utils_print("content of data_buf = %s\n", data_buf);

    data_msg_hdr         = &proxy_data_msg_hdr;
    res_string           = res_buf;
    desc_string          = desc_buf;

    data_msg_hdr->outer_header.payload_len = strlen("test msg");

    utils_print("outer_header.payload_len = %d\n", data_msg_hdr->outer_header.payload_len);
    utils_print("In %s, version = %d, type = %d\n", __func__, data_msg_hdr->outer_header.version, data_msg_hdr->outer_header.proxy_msg_type);
    DUMP_BUFFER_CONTENT(data_buf, 8, "%c");

    ret = scenario_msg_inject_frontend(engine, data_msg_hdr, data_buf, strlen(data_buf), MEMORY_ALLOC_SHARED, res_string, desc_string, desc_len);

    return FRONTEND_PROXY_PROCESS_OK;
}


int test_proxy_scenario_multi_type_msg_build_frontend(FrontendEngine *engine){
    device_msg_inject_frontend(engine);
    strategy_msg_inject_frontend(engine);
    session_msg_inject_frontend(engine);
    data_msg_inject_frontend(engine);

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Upper-layer scenario-based test function for the frontend proxy layer, which uniformly reads multi-type proxy messages from the shared memory RX queue 
 * and simulates the data reading process.
 * @details Designed based on the core responsibilities of "Scenario Functions", serving as a standardized entry for unit testing:
 * Automatically reads all core proxy message types that may exist in the queue, including device messages, strategy messages, session messages, and data messages 
 * (the actual types and quantities are uncertain, depending on the injected content);
 * Calls the underlying message reading functions (e.g., frontend_engine_rx_queue_get, scenario_msg_read) to complete the parsing and extraction of structured messages;
 * Obtains the shared memory RX queue handle from the input FrontendEngine global context (which must be initialized in advance), and reads messages from the queue following FIFO rules;
 * Does not require external input of message parameters (e.g., expected msg type, msg ID). All test verification logic (such as checking message structure validity, matching preset parameters) is 
 * based on the test scenarios (e.g., verifying that session messages contain preset session ID = 1, data messages match preset payload length) to ensure test consistency.
 * @param[in] engine Pointer to the FrontendEngine global context, which must meet the requirements in the document:
 * Must be successfully initialized via the engine_init function (returning FRONTEND_PROXY_PROCESS_OK) to ensure that resources such as the memory pool handle and 
 * shared memory RX queue (engine->rx_queue) are ready
 * The context must contain valid runtime configurations (e.g., shared memory queue size, message parsing rules) to avoid resource unavailability errors during message reading or parsing
 * @return int Follows the unified error code specification in the document, with return value meanings as follows:
 * FRONTEND_PROXY_PROCESS_OK: All existing types of proxy messages in the RX queue are successfully read and parsed;
 * FRONTEND_PROXY_PROCESS_ERROR: All abnormal scenarios are covered, including uninitialized FrontendEngine, NULL internal resources (e.g., rx_queue), empty shared memory RX queue (when messages 
 * are expected), and failed calls to underlying message reading/parsing functions
 * @note 1. Precondition: The engine must be initialized before calling this function, and messages must be pre-injected into the RX queue (e.g., via test_proxy_scenario_multi_type_msg_build_frontend) (refer to the normal scenario process in "Test Process - Message Reading Test" of "Frontend Proxy Layer Unit Test.doc"), otherwise a process error will be returned directly;
 * Message coverage: The current version supports reading 4 core message types, which may exist in any combination (one or more types) depending on the injection scenario. 
 * Device messages: Verify consistency with preset "device status query" commands;
 * Strategy messages: Verify consistency with preset "query strategy configuration" commands (matching the "strategy configuration command parsing" scenario);
 * Session messages: Verify consistency with preset "create session" (valid device ID) and "close session" (valid session ID) commands (matching the "session command parsing" scenario);
 * Data messages: Verify consistency with preset boundary scenarios (empty payload and maximum-length payload (4088 bytes));
 * Result feedback: The function internally uses error_print to print detailed logs of message reading/parsing (e.g., "Device message read successfully", "Data message parsing failed: 
 * invalid payload length"),
 * and the log format complies with the log specification for "message processing failure";
 * Subsequent verification: After message reading is completed, the function may trigger internal verification logic (e.g., checking message count, comparing with injected content) to confirm 
 * the correctness of the reading process, thus completing the full test link of "inject-read-verify".
 */
int test_proxy_scenario_msg_read_from_rx_queue_frontend(FrontendEngine *engine){
    struct SharedMemoryPoolQueue     *rx_queue, *tx_queue;
    struct BackendSessionQueue       *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                          *proxy_msg, **p_proxy_msg;
    uint8_t                          content[8];                  
    ProxyMsgHeader                   *proxy_msg_hdr;
    uint32_t                         msg_size;
    int                              ret;

    rx_queue = engine->rx_queue;
//    proxy_msg = content;
    p_proxy_msg = &proxy_msg;

    if(NULL == rx_queue){
        error_print("test_proxy_scenario_msg_read_from_rx_queue_frontend failed: the rx_queue of the engine is not initialized!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

#if 1
    proxy_msg = frontend_engine_rx_queue_get_msg(rx_queue, PROXY_MSG_HDR_PLUS_MAX_SIZE, &ret, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get_msg\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);
    frontend_proxy_msg_process(proxy_msg);

    proxy_msg = frontend_engine_rx_queue_get_msg(rx_queue, PROXY_MSG_HDR_PLUS_MAX_SIZE, &ret, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get_msg\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);
    frontend_proxy_msg_process(proxy_msg);

    proxy_msg = frontend_engine_rx_queue_get_msg(rx_queue, PROXY_MSG_HDR_PLUS_MAX_SIZE, &ret, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get_msg\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);
    frontend_proxy_msg_process(proxy_msg);

    proxy_msg = frontend_engine_rx_queue_get_msg(rx_queue, PROXY_MSG_HDR_PLUS_MAX_SIZE, &ret, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get_msg\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);
    frontend_proxy_msg_process(proxy_msg);
#endif

#if 0
    ret = frontend_engine_rx_queue_get(rx_queue, &proxy_msg, PROXY_MSG_HDR_PLUS_MAX_SIZE, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d,  address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);

//    DUMP_BUFFER_CONTENT(proxy_msg, msg_size, "%d");

    ret = frontend_engine_rx_queue_get(rx_queue, &proxy_msg, PROXY_MSG_HDR_PLUS_MAX_SIZE, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);
//    frontend_proxy_msg_process(proxy_msg);

    ret = frontend_engine_rx_queue_get(rx_queue, &proxy_msg, PROXY_MSG_HDR_PLUS_MAX_SIZE, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);

    ret = frontend_engine_rx_queue_get(rx_queue, &proxy_msg, PROXY_MSG_HDR_PLUS_MAX_SIZE, &msg_size);
    utils_print("In %s, after enter frontend_engine_rx_queue_get\n", __func__);
    utils_print("rx queue header is %d, tail is %d, msg_size is %d, address of msg is %p\n", rx_queue->header, rx_queue->tail, msg_size, proxy_msg);



#endif
    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Inject a device message into the frontend engine via the HyperAMP queue
 *
 * @details This function sends a device-related message from the backend to the frontend engine 
 *          by enqueuing it into the HyperAMP TX queue (i.e., the backend-to-frontend communication channel). 
 *          The message is formatted according to the HyperAMP protocol and will be consumed by the 
 *          frontend engine (running in seL4) during its next polling or notification cycle.
 *          Unlike direct in-process injection, this version relies on cross-environment shared-memory 
 *          messaging through HyperAMP.
 *
 * @param engine Pointer to the FrontendEngine instance that owns or manages the HyperAMP interface
 * @return int Return code indicating the result of the injection attempt:
 *         - FRONTEND_PROXY_PROCESS_OK: Message successfully enqueued into the HyperAMP TX queue
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to enqueue the message (e.g., queue full, 
 *                                         uninitialized HyperAMP context, invalid engine state)
 *
 * @note This function does not wait for the frontend to process the message; it only ensures 
 *       the message is placed in the shared queue. Delivery and handling are asynchronous.
 *       The actual message content and layout must conform to the agreed-upon HyperAMP device 
 *       message schema between Linux (backend) and seL4 (frontend).
 */
int device_msg_inject_frontend_hyperamp(FrontendEngine *engine){
    GeneralProxyMsgHeader *dev_msg_hdr;
//    DevMsgReport           dev_msg_resp;
    DevMsgMask            dev_msg_mask;
    int                   ret, desc_len = 100;
    uint8_t               **res_string;
    char                  *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};

    dev_msg_hdr          = &dev_enable_msg_hdr;

#if 0
    dev_msg_resp.status  = SESS_OP_STATUS_SUCCESS;
    dev_msg_resp.error   = SESS_OP_CODE_SUCCESS;
    dev_msg_resp.data    = 0xFF;
#endif

    dev_msg_mask.data   = 0xFF;


    res_string           = res_buf;
    desc_string          = desc_buf;

    utils_print("In %s, before enter scenario_msg_inject\n", __func__);
    ret = scenario_msg_inject_frontend(engine, dev_msg_hdr, &dev_msg_mask, sizeof(dev_msg_mask), MEMORY_ALLOC_AMPQUEUE, res_string, desc_string, desc_len);

    return ret;
}


/**
 * @brief Inject a strategy message into the frontend engine via the HyperAMP queue
 *
 * @details This function sends a strategy-related message from the backend to the frontend engine 
 *          by enqueuing it into the HyperAMP TX queue (i.e., the backend-to-frontend communication channel). 
 *          The message is formatted according to the HyperAMP protocol and will be consumed by the 
 *          frontend engine (running in seL4) during its next polling or notification cycle.
 *          Unlike direct in-process injection, this version relies on cross-environment shared-memory 
 *          messaging through HyperAMP.
 *
 * @param engine Pointer to the FrontendEngine instance that owns or manages the HyperAMP interface
 * @return int Return code indicating the result of the injection attempt:
 *         - FRONTEND_PROXY_PROCESS_OK: Message successfully enqueued into the HyperAMP TX queue
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to enqueue the message (e.g., queue full, 
 *                                         uninitialized HyperAMP context, invalid engine state)
 *
 * @note This function does not wait for the frontend to process the message; it only ensures 
 *       the message is placed in the shared queue. Delivery and handling are asynchronous.
 *       The actual message content and layout must conform to the agreed-upon HyperAMP strategy 
 *       message schema between Linux (backend) and seL4 (frontend).
 */
int strategy_msg_inject_frontend_hyperamp(FrontendEngine *engine){
    GeneralProxyMsgHeader   *strgy_msg_hdr;
    StrgyCMDEnableMessage   strgy;
    int                     ret, desc_len = 100;
    uint8_t                 **res_string;
    char                    *desc_string;
    uint8_t                 *res_buf[100] = {NULL};
    char                    desc_buf[100] = {0};

    strgy_msg_hdr          = &strgy_set_msg_hdr;
    strgy.strgy_para       = 0;

    res_string           = res_buf;
    desc_string          = desc_buf;

    ret = scenario_msg_inject_frontend(engine, strgy_msg_hdr, &strgy, sizeof(strgy), MEMORY_ALLOC_AMPQUEUE, res_string, desc_string, desc_len);

    return ret;
}


/**
 * @brief Inject a session message into the frontend engine via the HyperAMP queue
 *
 * @details This function sends a session-related message from the backend to the frontend engine 
 *          by enqueuing it into the HyperAMP TX queue (i.e., the backend-to-frontend communication channel). 
 *          The message is formatted according to the HyperAMP protocol and will be consumed by the 
 *          frontend engine (running in seL4) during its next polling or notification cycle.
 *          Unlike direct in-process injection, this version relies on cross-environment shared-memory 
 *          messaging through HyperAMP.
 *
 * @param engine Pointer to the FrontendEngine instance that owns or manages the HyperAMP interface
 * @return int Return code indicating the result of the injection attempt:
 *         - FRONTEND_PROXY_PROCESS_OK: Message successfully enqueued into the HyperAMP TX queue
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to enqueue the message (e.g., queue full, 
 *                                         uninitialized HyperAMP context, invalid engine state)
 *
 * @note This function does not wait for the frontend to process the message; it only ensures 
 *       the message is placed in the shared queue. Delivery and handling are asynchronous.
 *       The actual message content and layout must conform to the agreed-upon HyperAMP session 
 *       message schema between Linux (backend) and seL4 (frontend).
 */
int session_msg_inject_frontend_hyperamp(FrontendEngine *engine){
    GeneralProxyMsgHeader *sess_msg_hdr;
    SessIPv4Params sess_ipv4_paras;
    int ret, desc_len = 100;
    uint8_t **res_string;
    char *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};
    char *ip_port_string = "192.168.1.101:8888";

    sess_msg_hdr                            = &sess_create_msg_hdr;

    sess_ipv4_paras.transport_layer_proto   = SESS_UDP_PROTO;
    sess_ipv4_paras.device_selection        = 0xFF;
    IPV4_PORT_STR_TO_TUPLE(ip_port_string, sess_ipv4_paras.dest_endpoint);

    res_string           = res_buf;
    desc_string          = desc_buf;

    utils_print("In %s, version = %d, type = %d\n", __func__, sess_msg_hdr->outer_header.version, sess_msg_hdr->outer_header.proxy_msg_type);

    ret = scenario_msg_inject_frontend(engine, sess_msg_hdr, &sess_ipv4_paras, sizeof(sess_ipv4_paras), MEMORY_ALLOC_AMPQUEUE, res_string, desc_string, desc_len);

    return ret;
}


/**
 * @brief Inject a data message into the frontend engine via the HyperAMP queue
 *
 * @details This function sends a data-related message from the backend to the frontend engine 
 *          by enqueuing it into the HyperAMP TX queue (i.e., the backend-to-frontend communication channel). 
 *          The message is formatted according to the HyperAMP protocol and will be consumed by the 
 *          frontend engine (running in seL4) during its next polling or notification cycle.
 *          Unlike direct in-process injection, this version relies on cross-environment shared-memory 
 *          messaging through HyperAMP.
 *
 * @param engine Pointer to the FrontendEngine instance that owns or manages the HyperAMP interface
 * @return int Return code indicating the result of the injection attempt:
 *         - FRONTEND_PROXY_PROCESS_OK: Message successfully enqueued into the HyperAMP TX queue
 *         - FRONTEND_PROXY_PROCESS_ERROR: Failed to enqueue the message (e.g., queue full, 
 *                                         uninitialized HyperAMP context, invalid engine state)
 *
 * @note This function does not wait for the frontend to process the message; it only ensures 
 *       the message is placed in the shared queue. Delivery and handling are asynchronous.
 *       The actual message content and layout must conform to the agreed-upon HyperAMP data 
 *       message schema between Linux (backend) and seL4 (frontend).
 */
int data_msg_inject_frontend_hyperamp(FrontendEngine *engine){
    GeneralProxyMsgHeader *data_msg_hdr;
    uint8_t **res_string;
    char *desc_string;
    uint8_t               *res_buf[100] = {NULL};
    char                  desc_buf[100] = {0};
    char data_buf[100];
    int ret,  desc_len = 100;



    memset(data_buf, 0, sizeof(data_buf));
    snprintf(data_buf, sizeof(data_buf), "test msg");

    utils_print("strlen(test msg) = %d\n", strlen("test msg"));
    utils_print("content of data_buf = %s\n", data_buf);

    data_msg_hdr         = &proxy_data_msg_hdr;
    res_string           = res_buf;
    desc_string          = desc_buf;

    data_msg_hdr->outer_header.payload_len = strlen("test msg");

    utils_print("outer_header.payload_len = %d\n", data_msg_hdr->outer_header.payload_len);

    utils_print("In %s, version = %d, type = %d\n", __func__, data_msg_hdr->outer_header.version, data_msg_hdr->outer_header.proxy_msg_type);

    DUMP_BUFFER_CONTENT(data_buf, 8, "%c");

    ret = scenario_msg_inject_frontend(engine, data_msg_hdr, &data_buf, strlen(data_buf), MEMORY_ALLOC_AMPQUEUE, res_string, desc_string, desc_len);

    return FRONTEND_PROXY_PROCESS_OK;
}


int test_proxy_scenario_multi_type_msg_build_frontend_hyperamp(FrontendEngine *engine){
    int ret;
//    ret = device_msg_inject_frontend_hyperamp(engine);
//    ret = strategy_msg_inject_frontend_hyperamp(engine);
    ret = session_msg_inject_frontend_hyperamp(engine);
//    ret = data_msg_inject_frontend_hyperamp(engine);

    return ret;
}



/**
 * @brief Process the active Frontend-to-Backend session queue in the frontend proxy scenario
 * @details This function is designed to be executed after the frontend engine reads messages from the receive queue.
 * It accesses all active Frontend-to-Backend (f2b) sessions managed by the frontend engine,
 * retrieves the to-be-sent data from each active session in sequence, and send the data to 
 * the corresponding Backend side through the HyperAMP TX queue by calling sess_pool->data_process_f2b.
 *
 * @param[in] engine Pointer to a FrontendEngine structure that manages the active f2b sessions and related frontend resources
 * @return int Returns FRONTEND_PROXY_PROCESS_OK on successful processing of all active sessions; 
 *                     FRONTEND_PROXY_PROCESS_ERROR if any error occurs during processing (e.g., socket write failure, invalid session)
 */
int test_frontend_proxy_scenario_process_active_f2b_sess_queue(FrontendEngine *engine){
    struct FrontendSessionQueue      *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                         *proxy_msg;
    uint32_t                        msg_size;
    int                             ret;

    sess_pool       = engine->sess_pool;
    sess_pool_ops   = sess_pool->ops;

    FRONTEND_ENGINE_GET_F2B_QUEUE(engine, active_queue_f2b);

    TAILQ_FOREACH_SAFE(cur_sess, active_queue_f2b, entries_f2b, next_sess){
/*
 * Call the corresponding data_process_f2b function pointer in the session pool's operation set (sess_pool_ops), 
 * which attempts to send the frontend-to-backend (F2B) data maintained by the current session (cur_sess) 
 * through the HyperAMP TX queue. The data_process_f2b pointer points to the frontend_high_speed_data_process_f2b function.
 * 
 * The return value indicates three possible scenarios:
 *   - Returns FRONTEND_PROXY_PROCESS_OK: All data was sent successfully.
 *   - Returns FRONTEND_PROXY_PROCESS_AGAIN: Not all data was sent, and no errors occurred (retry is needed).
 *   - Returns FRONTEND_PROXY_PROCESS_ERROR: An error occurred during the data transmission process.
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

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Process the active Backend-to-Frontend session queue in the frontend proxy scenario
 * @details This function is designed for processing Backend-to-Frontend (b2f) traffic in the frontend proxy layer.
 * It accesses all active b2f sessions managed by the frontend engine,
 * calls the sess->event_callback function in sequence for each session,
 * and read the packets in the session's b2f message queue from the frontend-side shared memory RX queue.
 *
 * @param[in] engine Pointer to a FrontendEngine structure that manages the active b2f sessions and related frontend resources 
 *                   (including b2f message queues, HyperAMP TX queue, etc.)
 * @return int Returns FRONTEND_PROXY_PROCESS_OK on successful processing of all active sessions; 
 *                     FRONTEND_PROXY_PROCESS_ERROR if any error occurs during processing
 */
int test_frontend_proxy_scenario_process_active_b2f_sess_queue(FrontendEngine *engine){
    struct FrontendSessionQueue      *active_queue_f2b, *active_queue_b2f;
    struct FrontendSession           *cur_sess, *next_sess;
    struct FrontendSessionPool       *sess_pool;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint8_t                         *proxy_msg;
    uint32_t                        msg_size;
    int                             ret;

    FRONTEND_ENGINE_GET_B2F_QUEUE(engine, active_queue_b2f);

    sess_pool       = engine->sess_pool;
    sess_pool_ops   = sess_pool->ops;

    utils_print("In %s\n", __func__);

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
    }

    return FRONTEND_PROXY_PROCESS_OK;
}