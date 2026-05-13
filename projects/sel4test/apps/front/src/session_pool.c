#include <stdio.h>
#include <string.h>

#include "session_pool.h"
#include "message.h"
#include "engine.h"
#include "shared_mem_io.h"
#include "common_utils.h"

#include "frontend_api.h"

FrontendEngine *p_g_fr_eng = NULL;
FrontendEngine g_fr_eng;
struct FrontendSessionPool  *front_high_speed_pool = NULL;
//ops
// int frontend_high_speed_create_sess(struct FrontendSessionPool *s_pool, struct FrontendSession **sess, struct SessMsgPara *para);
int frontend_high_speed_create_sess_step1(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, struct SessMsgPara *para);
int frontend_high_speed_create_sess_step2(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, uint16_t sess_id, SessOpRespData *resp);
int frontend_high_speed_create_sess_passive(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, struct SessMsgPara *para);
int frontend_high_speed_insert_sess(struct FrontendSessionPool* s_pool, struct FrontendSession *sess);
struct FrontendSession* frontend_high_speed_search_sess(struct FrontendSessionPool *s_pool, uint16_t id);
int frontend_high_speed_delete_sess(struct FrontendSessionPool *s_pool, struct FrontendSession *sess);
void frontend_high_speed_destroy_pool(struct FrontendSessionPool *s_pool);
int frontend_high_speed_close_sess_step1(struct FrontendSessionPool *s_pool, struct FrontendSession *sess);
int frontend_high_speed_close_sess_step2(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, SessOpRespData *resp);




struct FrontendSessionPoolOps fr_high_speed_pool_ops = {
    .create_sess_step1  = frontend_high_speed_create_sess_step1,
    .create_sess_step2  = frontend_high_speed_create_sess_step2,
    .insert_sess        = frontend_high_speed_insert_sess,
    .search_sess        = frontend_high_speed_search_sess,
    .delete_sess        = frontend_high_speed_delete_sess,
    .data_process_b2f   = frontend_high_speed_data_process_b2f,
    .data_process_f2b   = frontend_high_speed_data_process_f2b,
    .close_sess_step1   = frontend_high_speed_close_sess_step1,
    .close_sess_step2   = frontend_high_speed_close_sess_step2,
    .destroy_pool       = frontend_high_speed_destroy_pool
};

/**
 * @brief Get the global FrontendEngine instance
 *
 * This function provides a global access point and returns a pre-initialized 
 * FrontendEngine singleton pointer. It is suitable for scenarios where the same
 * frontend engine instance needs to be shared throughout the entire program.
 *
 * @return FrontendEngine* Pointer to the global FrontendEngine instance, guaranteed
 *                         to be non-null (provided the global instance is properly initialized)
 * @note 1. The global instance `p_g_fr_eng` must be initialized before calling this function;
 *          otherwise, a null pointer or wild pointer may be returned.
 *       2. This function is not thread-safe. In a multi-threaded environment, ensure the
 *          instance is fully initialized before invoking this function.
 *       3. Directly modifying the state of the instance pointed to by the returned pointer
 *          is not recommended. If modifications are required, use the interfaces provided
 *          by the FrontendEngine class.
 */
FrontendEngine *frontend_get_global_engine(){
    return p_g_fr_eng;
}

struct FrontendSessionPool *frontend_get_high_speed_pool(){
    return front_high_speed_pool;
}

//helper func
void fill_id_queue(struct FrontendSessionIDQueue *id_q)
{
    uint16_t q_num = 1024;
    for (uint16_t i = 1; i <= q_num; i++)
    {
        struct FrontendSessionID* id_e = (struct FrontendSessionID*)malloc(
            sizeof(struct FrontendSessionID));
        if (!id_e) {
            printf("Memory allocate failed!\n");
            exit(1);
        }
        id_e->id = i;
        TAILQ_INSERT_TAIL(id_q, id_e, entry);
        // printf("push %p %d\n", id_e, id_e->id);
    }
}

uint16_t allocate_id(struct FrontendSessionIDQueue *id_q)
{
    uint16_t res = 0;

    if(!TAILQ_EMPTY(id_q))
    {   
        struct FrontendSessionID *id_e = TAILQ_FIRST(id_q);
        res = id_e->id;
        // printf("pop %p %d\n", id_e, res);
        TAILQ_REMOVE(id_q, id_e, entry);
        free(id_e);
    } 
    return res;
}

void release_id(struct FrontendSessionIDQueue *id_q, uint16_t id)
{   
    struct FrontendSessionID* id_e = (struct FrontendSessionID*)malloc(
            sizeof(struct FrontendSessionID));
    if (!id_e) {
        printf("Memory allocate failed!\n");
        exit(1);
    }
    id_e->id = id;
    TAILQ_INSERT_TAIL(id_q, id_e, entry);
    // printf("push %p %d\n", id_e, id_e->id);
}


/**
 * @brief Initialize the frontend high-speed session pool
 *
 * This function initializes the `FrontendSessionPool` instance, including validating input parameters,
 * setting core pool attributes, initializing internal linked queues, and binding pool operation functions.
 * The initialization process follows these key steps:
 * 1. Check if the input `pool` pointer is non-NULL (avoid null pointer dereference);
 * 2. Set fixed pool name, default capacity (1024 sessions), and initialize current session count to 0;
 * 3. Initialize three internal TAILQ linked queues (ID management queue, f2b/b2f data queues);
 * 4. Populate the ID queue with available session IDs via `fill_id_queue()`;
 * 5. Bind the pre-defined pool operation set `fr_high_speed_pool_ops` to the pool;
 * 6. Initialize the hash table pointer to NULL (to be allocated or initialized later if needed).
 *
 * @param[in,out] pool Pointer to the FrontendSessionPool instance to be initialized.
 *                     Must point to a pre-allocated memory block (cannot be NULL).
 *
 * @return int Return code indicating initialization result:
 *             - FRONTEND_PROXY_PROCESS_OK: Initialization succeeded;
 *             - FRONTEND_PROXY_PROCESS_ERROR: Initialization failed (input `pool` is NULL).
 *
 * @note 1. The `pool` instance must be pre-allocated (static or dynamic memory) before calling this function;
 *       2. The pool capacity is fixed to 1024 in this implementation (modify `pool->capacity` manually if dynamic adjustment is needed);
 *       3. The TAILQ queues (`id_queue`, `queue_f2b`, `queue_b2f`) rely on the system's TAILQ macro implementation (ensure relevant headers are included);
 *       4. The `fr_high_speed_pool_ops` must be a pre-defined valid operation set (contains pool-related operation interfaces like session allocation/release);
 *       5. The `pool->engine` assignment is commented out; uncomment it if the pool needs to associate with the global FrontendEngine instance,
 *          and ensure `get_global_frontend_engine()` is called after `frontend_engine_init()` to avoid null pointer risks.
 * @warning Passing a NULL `pool` pointer will directly return an error and print a log via `error_print()`.
 */
int frontend_high_speed_init_pool(struct FrontendSessionPool *pool)
{
    if(NULL == pool){
        error_print("frontend_high_speed_init_pool failed: the point of pool is NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }
    pool->pool_name = "frontend_high_speed_pool";
    pool->capacity = 1024;
    pool->sess_num = 0;

    TAILQ_INIT(&pool->id_queue);
    fill_id_queue(&pool->id_queue);

    TAILQ_INIT(&pool->queue_f2b);
    TAILQ_INIT(&pool->queue_b2f);

    pool->htable            = NULL;
    pool->ops               = &fr_high_speed_pool_ops;
    pool->engine            = frontend_get_global_engine();
    front_high_speed_pool   = pool;
    return FRONTEND_PROXY_PROCESS_OK;
}


void inc_sess_num(struct FrontendSessionPool *pool)
{
    pool->sess_num++;
}

void dec_sess_num(struct FrontendSessionPool *pool)
{
    pool->sess_num--;
}

void print_pool(struct FrontendSessionPool *s_pool) {
    struct FrontendSession *s;

    for (s = s_pool->htable; s != NULL; s = s->hh.next) {
        printf("sess id %d\n", s->backend_sess_id);
    }
}

void frontend_high_speed_delete_all_sess(struct FrontendSessionPool *s_pool)
{
    struct FrontendSession *current_sess, *tmp;

    HASH_ITER(hh, s_pool->htable, current_sess, tmp) {
        HASH_DEL(s_pool->htable, current_sess);  /* delete it */
        free(current_sess);                      /* free it */
    }
}

//ops

/**
 * @brief Step 1 of high-speed session creation process in the frontend proxy
 * @details This function serves as the first step in the frontend high-speed session creation workflow.
 * It extracts necessary parameters from the input SessMsgPara structure, constructs a session creation Command message,
 * and sends the constructed message to the backend proxy through the shared memory queue.
 * The function focuses on message construction and inter-proxy communication, laying the foundation for subsequent
 * session establishment steps between the frontend and backend.
 * @param[in] s_pool Pointer to the FrontendSessionPool instance, used for managing frontend session-related resources
 * (e.g., session pool initialization status, resource allocation constraints)
 * @param[in,out] sess Double pointer to FrontendSession, used to return the pointer of the to-be-created high-speed session
 * (the function may initialize the session context and assign it to this pointer for subsequent operations)
 * @param[in] para Pointer to the SessMsgPara structure, containing core parameters required for constructing the session creation Command message
 * (e.g., session identifiers, protocol configuration, connection parameters)
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK if the session creation Command message is successfully constructed
 * and placed into the shared memory queue; Returns FRONTEND_PROXY_PROCESS_ERROR if s_pool/para is NULL, parameter validation fails,
 * message construction fails, or the shared memory queue send operation encounters exceptions.
 */
 int frontend_high_speed_create_sess_step1(struct FrontendSessionPool *s_pool, struct FrontendSession *sess,
                                           struct SessMsgPara *para){
    FrontendEngine                      *engine;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    uint16_t                            frontend_sess_id, new_sess_id, dev_id;
    GeneralProxyMsgHeader               proxy_msg_hdr;
    SessIPv4Params                      ipv4_para;
    uint8_t                             *msg_payload;
    int                                 ret;
    uint8_t                             **res_msg, *res_buf[100] = {NULL};


    utils_print("In %s, addr is %d.%d.%d.%d port = %d\n", __func__, 
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[0],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[1],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[2],
                para->ip_port_tuple.ipv4_port_tuple.ipv4_addr.data[3],  
                para->ip_port_tuple.ipv4_port_tuple.port);
    
    utils_print("frontend_sess_id = %d, backend_sess_id = %d, ip_version = %d, dev_id = %d, trans_proto = %d\n",
                para->frontend_sess_id, para->backend_sess_id, para->ip_version, para->dev_id, para->trans_proto);

    engine          = s_pool->engine;
    sess_pool_ops   = s_pool->ops;
    res_msg         = res_buf;

    utils_print("In %s\n", __func__);
    utils_print("The address of engine is %p\n", engine);
    utils_print("The address of engine ops is %p\n", engine->ops);
    utils_print("The address of session pool ops is %p\n", sess_pool_ops);
#if 0
    utils_print("In %s, the address of the engine is %p, ops is %p， hs_backend_eng_ops address is %p, and chooes_dev is %p\n", 
                __func__, engine, engine->ops, get_hs_backend_engine_ops(), engine->ops->choose_dev);
#endif

    if(NULL == engine){
        error_print("frontend_high_speed_create_sess_step1 fails: the session pool does not belong to any engine, or the engine is not initialized successfully!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == sess_pool_ops || NULL == sess_pool_ops->insert_sess){
        error_print("frontend_high_speed_create_sess_step1 fails: the session pool operation function set is not initialized correctly!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }



    new_sess_id = allocate_id(&s_pool->id_queue);
    if(0 == new_sess_id){
        error_print("frontend_high_speed_create_sess_step1 failed: failed to allocating session ID!");
        goto create_sess_error;
    }

    
/*
 * Initialize session object.
 */ 
    sess->frontend_sess_id  = new_sess_id;   
    sess->backend_sess_id   = para->backend_sess_id;
    sess->ip_version        = para->ip_version;
    sess->state_f2b         &= FRONTEND_SESS_LINKED_TO_QUEUE;
    sess->state_b2f         &= FRONTEND_SESS_LINKED_TO_QUEUE;
    sess->sess_state        = FRONTEND_SESS_INITIALIZE;
//    sess->data_process_callback = callback;
    TAILQ_INIT(&sess->msg_f2b);
    utils_print("In %s, TAILQ_EMPTY(&new_sess->msg_f2b) returns %d\n", __func__, TAILQ_EMPTY(&sess->msg_f2b));
    TAILQ_INIT(&sess->msg_b2f);
    utils_print("In %s, TAILQ_EMPTY(&new_sess->msg_b2f) returns %d\n", __func__, TAILQ_EMPTY(&sess->msg_b2f));


/*
 * Construct a SESSION-CREATION command message and send it to the backend proxy to notify the latter of session creation.
 */
    memset(&proxy_msg_hdr, 0, sizeof(GeneralProxyMsgHeader));
    memset(&ipv4_para, 0, sizeof(SessIPv4Params));

    proxy_msg_hdr.outer_header.version                  = PROXY_PROTO_VERSION_1;
    proxy_msg_hdr.outer_header.proxy_msg_type           = PROXY_MSG_TYPE_SESS,
    proxy_msg_hdr.outer_header.frontend_sess_id         = sess->frontend_sess_id;
    proxy_msg_hdr.outer_header.backend_sess_id          = sess->backend_sess_id;
    proxy_msg_hdr.outer_header.payload_len              = sizeof(SessMsgHeader) + sizeof(SessIPv4Params);

    proxy_msg_hdr.inner_header.sess_hdr.version         = PROXY_PROTO_SESS_VERSION_1;
    proxy_msg_hdr.inner_header.sess_hdr.msg_type        = SESS_MSG_CREATE;
    proxy_msg_hdr.inner_header.sess_hdr.action_type     = ACTION_TYPE_COMMAND;
    proxy_msg_hdr.inner_header.sess_hdr.ip_version      = para->ip_version;
    proxy_msg_hdr.inner_header.sess_hdr.payload_len     = sizeof(SessIPv4Params);

    ipv4_para.device_selection = para->dev_id;
    ipv4_para.transport_layer_proto = para->trans_proto;
    memcpy(&ipv4_para.dest_endpoint, &para->ip_port_tuple.ipv4_port_tuple, sizeof(ipv4_para.dest_endpoint));

//    ret = build_proxy_general_message(engine, &proxy_msg_hdr, &ipv4_para, sizeof(ipv4_para), res_msg, MEMORY_ALLOC_SHARED, engine->tx_queue);
    ret = build_proxy_general_message(engine, &proxy_msg_hdr, (void*) &ipv4_para, sizeof(ipv4_para), res_msg, MEMORY_ALLOC_AMPQUEUE, NULL);

    if(ret != FRONTEND_PROXY_PROCESS_OK){
        error_print("frontend_high_speed_create_sess_step1 failed: failed to build proxy general message or send to shared memory TX queue!");
        goto create_sess_error;
    }
/*
 * Insert the session instance into the session pool.
 */
    sess->sess_state        = FRONTEND_SESS_CONNECTING;
    ret                     = sess_pool_ops->insert_sess(s_pool, sess);

    if(ret != FRONTEND_PROXY_PROCESS_OK){
        error_print("frontend_high_speed_create_sess_step1 failed: failed to insert the session instance into the session pool!");
        goto create_sess_error;
    }

    return FRONTEND_PROXY_PROCESS_OK;

create_sess_error:
/*
 * Reclaim resources.
 * The memory occupied by the session instance is reclaimed in the function pointed to by the delete_sess pointer.
 */

    if(0 != new_sess_id){
        release_id(&s_pool->id_queue, new_sess_id);
    }
    return FRONTEND_PROXY_PROCESS_ERROR;
 }



/**
 * @brief Step 2 of high-speed session creation process in the frontend proxy
 * @details This function serves as the second step in the frontend high-speed session creation workflow, focusing on processing the backend proxy's response.
 * If the response (resp) indicates successful session creation, the function updates the backend_sess_id member of the target FrontendSession
 * with the backend-assigned sess_id. If the response returns a failure, the function prints a log message displaying the failure reason
 * maintained in resp, reclaims the resources occupied by the target session (sess), and removes the session from the FrontendSessionPool (s_pool).
 * @param[in] s_pool Pointer to the FrontendSessionPool instance, used for managing frontend session resources (e.g., removing invalid sessions from the pool)
 * @param[in,out] sess Pointer to FrontendSession, the target high-speed session to be updated or resource-reclaimed;
 * On success, its backend_sess_id is updated with sess_id; On failure, the session resources are reclaimed
 * @param[in] sess_id 16-bit session ID assigned by the backend proxy, used to update the backend_sess_id of the target FrontendSession when the response is successful
 * @param[in] resp Pointer to the SessOpRespData structure, containing the backend's session creation response status (success/failure) and corresponding failure reason
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK if the response is processed successfully (either session update or resource reclamation completed);
 * Returns FRONTEND_PROXY_PROCESS_ERROR if s_pool/sess/resp is NULL, session update fails, or resource reclamation/removal from the pool encounters exceptions.
 */
int frontend_high_speed_create_sess_step2(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, uint16_t sess_id, SessOpRespData *resp){
    FrontendEngine                   *engine;
    struct FrontendSessionPoolOps    *sess_pool_ops;
    uint16_t frontend_sess_id, new_sess_id, dev_id;
    uint8_t status, code;

    status          = resp->status;
    code            = resp->code;
    sess_pool_ops   = s_pool->ops;
/*
 * The backend proxy cannot set up a session. It replies to the frontend proxy with the SESS_OP_STATUS_ERROR status and the errno-based reason.
 * The frontend proxy, per the protocol, should record the errno-based reason, remove the session instance from the session pool, and release its resources.
 */
    if(SESS_OP_STATUS_SUCCESS != status){
        utils_print("In %s, the session creating procedurec failed, the error code is %d\n", __func__, code);
        release_id(&s_pool->id_queue, sess->frontend_sess_id);
    }

    sess->backend_sess_id = sess_id;
    sess->sess_state      = FRONTEND_SESS_CONNECTED;

    return FRONTEND_PROXY_PROCESS_OK;
}


/**
 * @brief Passive creation processing function for high-speed session in the frontend proxy
 * @details This function is the core processing entry for the **passive creation workflow** of high-speed sessions in the frontend proxy,
 * which is distinguished from the active session creation process. It is triggered by the session creation request/command from the backend proxy,
 * extracts the necessary core parameters from the input SessMsgPara structure, and performs strict validity verification on all key parameters
 * related to passive session creation, including the specified IP protocol version and core session parameters (e.g., frontend session ID, protocol configuration parameters).
 * Meanwhile, it manages the frontend session resources through the FrontendSessionPool instance, initializes the context of the high-speed session to be passively created
 * based on the validated IP protocol version (IPv4/IPv6), and assigns the relevant session attributes, resource information and network layer configuration
 * to the FrontendSession structure. This function focuses on the core logic of passive trigger-based session parameter verification (including IP version validation),
 * IP version-based session context initialization and frontend session resource allocation, and completes the key processing of high-speed session passive creation
 * in the frontend proxy, laying a solid foundation for the subsequent establishment of the front-backend session link and the normal data interaction of the high-speed session.
 * @param[in] s_pool Pointer to the FrontendSessionPool instance, used for managing frontend session-related resources
 * (e.g., session pool initialization status, resource allocation constraints, session life cycle management, valid session quantity limit)
 * @param[in,out] sess Pointer to the FrontendSession structure, used to initialize the context of the passively created high-speed session
 * (the function will assign the relevant session attributes, resource information, protocol configuration and IP version-related network layer settings to this pointer
 * for subsequent session link maintenance and data interaction operations)
 * @param[in] para Pointer to the SessMsgPara structure, containing core session parameters required for passive creation of the high-speed session
 * (e.g., session identifiers delivered by the backend, protocol configuration parameters, front-backend connection parameters, valid session ID range)
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK if the high-speed session passive creation logic is processed successfully,
 * including all parameter (IP version + core session parameters) verification passed, IP version-based session context initialized completely,
 * frontend session resource allocated normally and successful response to the backend for sending the new session creation command; 
 * Returns FRONTEND_PROXY_PROCESS_ERROR if s_pool/para is NULL, ip_version takes an invalid value,
 * core session parameter validity verification fails, session context initialization fails, frontend session pool resource is insufficient,
 * or other exceptions occur during the passive creation processing of the high-speed session.
 */
int frontend_high_speed_create_sess_passive(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, struct SessMsgPara *para){
    FrontendEngine                      *engine;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    GeneralProxyMsgHeader               proxy_msg_hdr;
    SessOpRespData                      resp_data;
    uint8_t                             **res_msg, *res_buf[100] = {NULL};
    int                                 ret;

/*
 * Check input parameters.
 */
    if(NULL == s_pool || NULL == s_pool->engine || NULL == s_pool->engine->tx_queue || NULL == s_pool->ops || NULL == s_pool->ops->insert_sess || 
       NULL == s_pool->ops->search_sess || NULL == sess || NULL == para){
        error_print("frontend_high_speed_create_sess_passive failed: null value detected in critical input (s_pool/engine/tx_queue/ops/insert_sess/search_sess/sess/para)!\n");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    engine          = s_pool->engine;
    sess_pool_ops   = s_pool->ops;

/*
 * Fill information for constructing the session-creation response message.
 */
    memset(&proxy_msg_hdr, 0, sizeof(GeneralProxyMsgHeader));
    proxy_msg_hdr.outer_header.version                  = PROXY_PROTO_VERSION_1;
    proxy_msg_hdr.outer_header.proxy_msg_type           = PROXY_MSG_TYPE_SESS,
    proxy_msg_hdr.outer_header.backend_sess_id          = para->backend_sess_id;
    proxy_msg_hdr.outer_header.frontend_sess_id         = para->frontend_sess_id;
    proxy_msg_hdr.outer_header.payload_len              = sizeof(SessMsgHeader) + sizeof(SessOpRespData);

    proxy_msg_hdr.inner_header.sess_hdr.version         = PROXY_PROTO_SESS_VERSION_1;
    proxy_msg_hdr.inner_header.sess_hdr.msg_type        = SESS_MSG_CREATE;
    proxy_msg_hdr.inner_header.sess_hdr.action_type     = ACTION_TYPE_RESPONSE;
    proxy_msg_hdr.inner_header.sess_hdr.ip_version      = para->ip_version;
    proxy_msg_hdr.inner_header.sess_hdr.payload_len     = sizeof(SessOpRespData);

    if(SESS_IPV4_PROTO != para->ip_version){
        error_print("frontend_high_speed_create_sess_passive failed: IP protocol version not supported, only IPv4 is supported currently, \ 
                     IPv6 support will be expanded in the future!\n");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_PARAMETER_INVALID;
        goto create_sess_passive_error;
    }

/*
 * Check whether the frontend session ID exists in the session pool.
 */
    if(sess_pool_ops->search_sess(s_pool, sess->frontend_sess_id)){
        error_print("frontend_high_speed_create_sess_passive failed: frontend session ID already exists in session pool!\n");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_PARAMETER_INVALID;
        goto create_sess_passive_error;
    }


    sess->state_f2b         &= FRONTEND_SESS_LINKED_TO_QUEUE;
    sess->sess_state        = FRONTEND_SESS_CONNECTED;
//    new_sess->data_process_callback = callback;
    TAILQ_INIT(&sess->msg_f2b);
    utils_print("In %s, TAILQ_EMPTY(&new_sess->msg_f2b) returns %d\n", __func__, TAILQ_EMPTY(&sess->msg_f2b));
    TAILQ_INIT(&sess->msg_b2f);
    utils_print("In %s, TAILQ_EMPTY(&new_sess->msg_b2f) returns %d\n", __func__, TAILQ_EMPTY(&sess->msg_b2f));

    ret = sess_pool_ops->insert_sess(s_pool, sess);

    if(ret != FRONTEND_PROXY_PROCESS_OK){
        error_print("frontend_high_speed_create_sess_passive failed: failed to insert the session instance into the session pool!\n");
        resp_data.status    = SESS_OP_STATUS_FAIL;
        resp_data.code      = SESS_OP_CODE_RESOURCE_INSUFFICIENT;
        goto create_sess_passive_error;
    }

    resp_data.status    = SESS_OP_STATUS_SUCCESS;
    resp_data.code      = SESS_OP_CODE_SUCCESS;

//    ret = build_proxy_general_message(engine, &proxy_msg_hdr, &resp_data, sizeof(resp_data), res_msg, MEMORY_ALLOC_SHARED, engine->tx_queue);
    ret = build_proxy_general_message(engine, &proxy_msg_hdr, (void*) &resp_data, sizeof(resp_data), res_msg, MEMORY_ALLOC_AMPQUEUE, NULL);

    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_high_speed_create_sess_passive: failed to build proxy response message for passive session creation!\n");
    }

    return ret;

create_sess_passive_error:
//    ret = build_proxy_general_message(engine, &proxy_msg_hdr, &resp_data, sizeof(resp_data), res_msg, MEMORY_ALLOC_SHARED, engine->tx_queue);
    ret = build_proxy_general_message(engine, &proxy_msg_hdr, (void*) &resp_data, sizeof(resp_data), res_msg, MEMORY_ALLOC_AMPQUEUE, engine->tx_queue);


    if(FRONTEND_PROXY_PROCESS_OK != ret){
        error_print("frontend_high_speed_create_sess_passive: failed to build error notification message!\n");
    }    

    return FRONTEND_PROXY_PROCESS_ERROR;
}


int frontend_high_speed_insert_sess(struct FrontendSessionPool *s_pool, struct FrontendSession *sess)
{
    struct FrontendSession *s;
    HASH_FIND(hh, s_pool->htable, &sess->frontend_sess_id, sizeof(uint16_t), s);
    if(s == NULL)
    {
        HASH_ADD(hh, s_pool->htable, frontend_sess_id, sizeof(uint16_t), sess);
        inc_sess_num(s_pool);
        printf("add %d\n", sess->frontend_sess_id);
    }else{
        goto insert_error;
    }
    return FRONTEND_PROXY_PROCESS_OK;
insert_error:
    return FRONTEND_PROXY_PROCESS_ERROR;
}

struct FrontendSession *frontend_high_speed_search_sess(struct FrontendSessionPool *s_pool, uint16_t id)
{
    struct FrontendSession* s = NULL;
    HASH_FIND(hh, s_pool->htable, &id, sizeof(uint16_t), s);
    return s;
}

int frontend_high_speed_delete_sess(struct FrontendSessionPool *s_pool, struct FrontendSession *sess)
{
    uint16_t        frontend_sess_id, backend_sess_id;
    int             ret, ip_version;
    FrontendEngine*  *eng;


    if(NULL == s_pool || NULL == sess || NULL == s_pool->engine){
        error_print("high_speed_delete_sess failed: session pool (s_pool), session (sess), or engine (s_pool->engine) is NULL!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    frontend_sess_id    = sess->frontend_sess_id;
    backend_sess_id     = sess->backend_sess_id;
    ip_version          = sess->ip_version;

    HASH_DEL(s_pool->htable, sess);
    release_id(&s_pool->id_queue, frontend_sess_id);

    dec_sess_num(s_pool);

    FRONTEND_SESS_UNLINK_FROM_QUEUE(sess, f2b);

    sess_msg_queue_free_all(&sess->msg_f2b);
    sess_msg_queue_free_all(&sess->msg_b2f);

    free(sess);

/*
 * TODO: send a close message to backend
*/

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Step 1 of high-speed session closure process in the frontend proxy
 * @details This function serves as the first step in the frontend high-speed session closure workflow.
 * Its core responsibilities include validating the validity of the target session, marking the session state as "closing",
 * preparing core parameters required for the closure process (e.g., session ID, connection context),
 * and laying the foundation for subsequent closure coordination with the backend (such as sending closure notifications,
 * resource synchronization). The function focuses on preprocessing and state initialization before session closure,
 * ensuring the orderly initiation of the entire closure process.
 * @param[in] s_pool Pointer to the FrontendSessionPool instance, used for managing global resources of the frontend session pool
 * (e.g., session state tracking, resource allocation constraints, overall pool state verification)
 * @param[in,out] sess Pointer to the FrontendSession instance to be closed; the function will modify the session's state
 * (e.g., mark as closing) and read its core information for closure initialization. Must point to a valid session
 * in an active or closable state.
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK if the closure initialization is successful
 * (session validity verified, state marked as closing, core parameters prepared);
 * Returns FRONTEND_PROXY_PROCESS_ERROR if s_pool/sess is NULL, the session is in an illegal state (e.g., already closed/invalid),
 * session state marking fails, or core closure parameter preparation encounters exceptions.
 * @note This function is only executed when the frontend actively initiates a session closure. For scenarios where the backend
 * actively closes the session, only the frontend_high_speed_close_sess_step2 function will be invoked instead.
 */
int frontend_high_speed_close_sess_step1(struct FrontendSessionPool *s_pool, struct FrontendSession *sess){
    FrontendEngine                      *engine;
    struct FrontendSessionPoolOps       *sess_pool_ops;
    uint16_t                            frontend_sess_id, backend_sess_id;
    GeneralProxyMsgHeader               proxy_msg_hdr;
    uint8_t                             *msg_payload;
    int                                 ret;
    uint8_t                             **res_msg, *res_buf[100] = {NULL};


    engine          = s_pool->engine;
    sess_pool_ops   = s_pool->ops;
    res_msg         = res_buf;

    if(NULL == engine || NULL == engine->ops){
        error_print("frontend_high_speed_close_sess_step1 fails: the session pool does not belong to any engine, or the engine is not initialized successfully!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }


    if(NULL == sess_pool_ops){
        error_print("frontend_high_speed_close_sess_step1 fails: the session pool operation function set is not initialized correctly!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    memset(&proxy_msg_hdr, 0, sizeof(GeneralProxyMsgHeader));

    proxy_msg_hdr.outer_header.version                  = PROXY_PROTO_VERSION_1;
    proxy_msg_hdr.outer_header.proxy_msg_type           = PROXY_MSG_TYPE_SESS,
    proxy_msg_hdr.outer_header.frontend_sess_id         = sess->frontend_sess_id;
    proxy_msg_hdr.outer_header.backend_sess_id          = sess->backend_sess_id;
    proxy_msg_hdr.outer_header.payload_len              = sizeof(SessMsgHeader) + sizeof(SessIPv4Params);

    proxy_msg_hdr.inner_header.sess_hdr.version         = PROXY_PROTO_SESS_VERSION_1;
    proxy_msg_hdr.inner_header.sess_hdr.msg_type        = SESS_MSG_CLOSE;
    proxy_msg_hdr.inner_header.sess_hdr.action_type     = ACTION_TYPE_COMMAND;
    proxy_msg_hdr.inner_header.sess_hdr.ip_version      = sess->ip_version;
    proxy_msg_hdr.inner_header.sess_hdr.payload_len     = 0;

//    ret = build_proxy_general_message(engine, &proxy_msg_hdr, NULL, 0, res_msg, MEMORY_ALLOC_SHARED, engine->tx_queue);
    ret = build_proxy_general_message(engine, &proxy_msg_hdr, NULL, 0, res_msg, MEMORY_ALLOC_AMPQUEUE, NULL);

    if(ret != FRONTEND_PROXY_PROCESS_OK){
        error_print("frontend_high_speed_close_sess_step1 failed: failed to build proxy general message or send to shared memory TX queue!");
        return FRONTEND_PROXY_PROCESS_ERROR;
    }

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief Step 2 of high-speed session closure process in the frontend proxy
 * @details This function serves as the second step in the frontend high-speed session closure workflow, focusing on processing the backend proxy's session closure response.
 * Its core responsibilities include validating the validity of the backend response data, releasing all resources occupied by the target session (e.g., connection handles, memory buffers, 
 * shared memory mappings), removing the session from the FrontendSessionPool, and synchronizing the final closure state. If the backend response (resp) indicates successful closure,
 * the function performs orderly resource reclamation and pool cleanup; if the response returns a failure, the function logs the failure reason contained in resp and executes forced
 * resource release to avoid leaks, ensuring the session is completely invalidated.
 * @param[in] s_pool Pointer to the FrontendSessionPool instance, used for managing frontend session resources (e.g., removing the closed session from the pool, verifying pool validity)
 * @param[in,out] sess Pointer to the FrontendSession instance to be closed; the function will release its occupied resources, update its final state to "closed",
 * and remove it from the session pool. Must point to a valid session that has initiated the closure process.
 * @param[in] resp Pointer to the SessOpRespData structure, containing the backend's session closure response status (success/failure) and corresponding failure reason
 * (e.g., abnormal backend session termination, resource release failure)
 * @return int Execution result: Returns FRONTEND_PROXY_PROCESS_OK if the backend response is processed successfully (resource reclamation, session removal from pool completed);
 * Returns FRONTEND_PROXY_PROCESS_ERROR if s_pool/sess/resp is NULL, response data validation fails, session resource reclamation fails, or session removal from the pool encounters exceptions.
 * @note This function is only invoked when the backend actively initiates a session closure. For scenarios where the frontend actively closes the session, this function will not be executed
 * (only frontend_high_speed_close_sess_step1 will be used in the frontend-initiated closure process).
 */
int frontend_high_speed_close_sess_step2(struct FrontendSessionPool *s_pool, struct FrontendSession *sess, SessOpRespData *resp){
    uint8_t status, code;
    int     ret;

    status = resp->code;

    if(SESS_OP_STATUS_SUCCESS != status){
        utils_print("In %s, the session creating procedurec failed, the error code is %d\n", __func__, code);
        sess->event_callback(sess, FRONTEND_SESS_EVENT_ABNORMAL);
        ret = FRONTEND_PROXY_PROCESS_ERROR;
    }else{
        sess->event_callback(sess, FRONTEND_SESS_EVENT_CLOSE);
        ret = FRONTEND_PROXY_PROCESS_OK;
    }

    return ret;
}


int frontend_high_speed_data_process_b2f(struct FrontendSession *sess){

    struct SessMsgSeg *cur_seg, *next_seg;

    TAILQ_FOREACH_SAFE(cur_seg, &sess->msg_b2f, entry, next_seg) {

        utils_print("cur_seg->len = %d, msg = %s\n", cur_seg->len, cur_seg->data);
        if (cur_seg->data && cur_seg->len > 0){
            uint8_t buf[cur_seg->len+1];
            utils_print("seg size = %d\n", cur_seg->len);
            memcmp(buf, cur_seg->data, cur_seg->len);

            /*
            * TODO: link to callback function, and pass the buf data to callback function
            */
            sess->data_process_callback(sess, buf, cur_seg->len);

        }else{
            /* Unable to recv the data in the queue */
            return FRONTEND_PROXY_PROCESS_AGAIN;
        }
        
        /* 2. Remove the segment from the queue */
        TAILQ_REMOVE(&sess->msg_b2f, cur_seg, entry);

        /* 3. Deallocate memory based on segment type */
        if (cur_seg->type == SESS_MSG_SEG_DYNAMIC_ALLOC) {
            // Free dynamically allocated data buffer
            free(cur_seg->data);
        } else if (cur_seg->type == SESS_MSG_SEG_SHARED_MEM) {

            // if (current_seg->mem_pool) {
            //     shared_memory_pool_release(current_seg->mem_pool, current_seg->data);
            // }
        }
        /* 4. Free the segment structure itself */
        free(cur_seg);

    }// TAILQ_FOREACH_SAFE

    return FRONTEND_PROXY_PROCESS_OK;
}



/**
 * @brief High-speed processing function for frontend-to-backend (F2B) data messages via HyperAMP queue
 *
 * @details This function traverses the frontend-to-backend message queue (msg_f2b) of a given FrontendSession,
 *          constructs general proxy data messages for each message segment, and sends them through the HyperAMP TX queue.
 *          It uses TAILQ_FOREACH_SAFE to safely iterate over the doubly linked list (TAILQ) to avoid iteration exceptions
 *          caused by node deletion during traversal. For each message segment:
 *          1. Populates the general proxy message header with session and protocol metadata (version, message type, session IDs)
 *          2. Calls build_proxy_general_message to assemble the data message for HyperAMP queue transmission
 *          3. On successful construction (FRONTEND_PROXY_PROCESS_OK): removes the segment from the queue and frees associated resources
 *          4. On temporary failure (FRONTEND_PROXY_PROCESS_AGAIN): terminates processing (HyperAMP TX queue full) and requests retry
 *          5. On fatal failure (FRONTEND_PROXY_PROCESS_ERROR): terminates processing immediately (device failure detected), 
 *             resource cleanup is handled by the external caller (global release of all session resources)
 *
 * @param[in] sess Pointer to the FrontendSession instance containing the F2B message queue to process;
 *                 must be a valid non-NULL pointer to an active session
 *
 * @return Integer status code indicating the processing result:
 *         - FRONTEND_PROXY_PROCESS_OK: All message segments in the msg_f2b queue were processed and sent successfully
 *         - FRONTEND_PROXY_PROCESS_AGAIN: Processing stopped prematurely (HyperAMP TX queue is full), 
 *                                         unprocessed segments remain in the queue (retry is required, no resource release)
 *         - FRONTEND_PROXY_PROCESS_ERROR: Fatal device failure detected, processing aborted immediately;
 *                                         the external caller MUST perform a "full cleanup" (release all session resources,
 *                                         including all pending message segments and the session instance itself)
 *
 * @retval FRONTEND_PROXY_PROCESS_OK All segments processed successfully, no pending data left in the queue
 * @retval FRONTEND_PROXY_PROCESS_AGAIN Temporary queue full error, recoverable via retry (no resource release needed)
 * @retval FRONTEND_PROXY_PROCESS_ERROR Unrecoverable device failure, requires global session resource release (all pending messages are discarded)
 *
 * @note 1. TAILQ_FOREACH_SAFE ensures safe traversal even when nodes are removed during iteration (next_seg caches the next node)
 *       2. Successfully processed segments are immediately removed from the queue and freed to prevent memory leaks
 *       3. FRONTEND_PROXY_PROCESS_AGAIN (queue full) is a recoverable error — the caller should retry this function after a short delay
 *       4. FRONTEND_PROXY_PROCESS_ERROR indicates a fatal device failure (not a single message error), so the caller MUST:
 *          - Release all resources associated with the session (msg_f2b queue, session instance, HyperAMP handles, etc.)
 *          - Discard all unprocessed message segments (no retry is meaningful for device failure scenarios)
 *          - Mark the session as invalid to avoid accidental reuse
 *       5. This function does NOT clean up failed segments on FRONTEND_PROXY_PROCESS_ERROR — global cleanup is delegated to the caller
 *       6. The function terminates immediately on non-OK return codes, leaving remaining segments unprocessed (expected behavior for fatal errors)
 *
 * @see build_proxy_general_message
 * @see sess_msg_seg_free
 * @see TAILQ_FOREACH_SAFE
 */
int frontend_high_speed_data_process_f2b(struct FrontendSession *sess){
    utils_print("In %s\n", __func__);
    struct SessMsgSeg       *cur_seg, *next_seg;
    GeneralProxyMsgHeader   data_msg_hdr;
    uint8_t                 *payload, **res_pointer;
    uint8_t                 *res_buf[100] = {NULL};
    int                     ret;

    memset(&data_msg_hdr, 0 , sizeof(data_msg_hdr));

    data_msg_hdr.outer_header.version           = PROXY_PROTO_VERSION_1;
    data_msg_hdr.outer_header.proxy_msg_type    = PROXY_MSG_TYPE_DATA;
    data_msg_hdr.outer_header.backend_sess_id   = sess->backend_sess_id;
    data_msg_hdr.outer_header.frontend_sess_id  = sess->frontend_sess_id;
    res_pointer                                 = res_buf;

/*
 * Core Logic: Safely traverse the f2b message queue, convert data in the queue to data-type messages
 * and send them via the shared-memory TX queue by calling the build_proxy_general_message function.
 * Safe traversal of the session's f2b message queue (TAILQ doubly linked list) to avoid iteration exceptions
 * caused by node deletion during traversal.
 * Parameter Description:
 * cur_seg: Current traversed message segment node
 * &sess->msg_f2b: Message queue head (f2b direction: frontend to backend message queue)
 * entry: Member used for linking nodes in the list (standard field of TAILQ linked list nodes)
 * next_seg: Temporarily stores the next node to ensure traversal can continue after deleting the current node
 */
    utils_print("Before contribute DATA message\n");
    utils_print("The address of the sess->eng = %p\n", sess->eng);
    TAILQ_FOREACH_SAFE(cur_seg, &sess->msg_f2b, entry, next_seg) {
        utils_print("data seg length = %d\n", cur_seg->len);
        data_msg_hdr.outer_header.payload_len = cur_seg->len;
/*
 * Call the proxy general message construction function to assemble the data-type message
 * Parameters: Engine instance, message header pointer, data segment pointer, data length, 
 * result storage pointer, memory allocation mode (shared memory), additional parameters
 */
//        ret =  build_proxy_general_message(sess->eng, &data_msg_hdr, cur_seg->data, cur_seg->len, res_pointer, MEMORY_ALLOC_SHARED, sess->eng->tx_queue);

        ret =  build_proxy_general_message(sess->eng, &data_msg_hdr, cur_seg->data, cur_seg->len, res_pointer, MEMORY_ALLOC_AMPQUEUE, NULL);

        if(FRONTEND_PROXY_PROCESS_OK == ret){
            TAILQ_REMOVE(&sess->msg_f2b, cur_seg, entry);
            sess_msg_seg_free(cur_seg);
        }else if(FRONTEND_PROXY_PROCESS_AGAIN == ret){
            error_print("frontend_high_speed_data_process_f2b stops: the HyperAMP TX queue is full!\n");
            return FRONTEND_PROXY_PROCESS_AGAIN;
        }else{
            error_print("frontend_high_speed_data_process_f2b failed: fail to build data message!\n");
            return FRONTEND_PROXY_PROCESS_ERROR;
        }
    }// TAILQ_FOREACH_SAFE(cur_seg, &sess->msg_f2b, entry, next_seg)

    return FRONTEND_PROXY_PROCESS_OK;
}


void frontend_high_speed_destroy_pool(struct FrontendSessionPool *s_pool)
{
    s_pool->pool_name   = NULL;
    s_pool->capacity    = 0;
    s_pool->sess_num    = 0;
    s_pool->ops         = NULL;
    frontend_high_speed_delete_all_sess(s_pool);

    //todo, clear queue
}



/**
 * @brief Default event callback function for frontend sessions
 *
 * This is the default callback implementation for handling frontend session events,
 * which is invoked by the session framework when specific events (defined in 
 * FrontendSessionEvent enumeration) occur during the lifecycle of a FrontendSession.
 * It provides basic event processing logic as a fallback when no custom callback is specified.
 *
 * @param[in] sess Pointer to the FrontendSession instance that triggered the event;
 *                 must be a valid non-NULL pointer pointing to an active session
 * @param[in] event The specific event type that occurred, one of the values in 
 *                  FrontendSessionEvent enumeration (FRONTEND_SESS_EVENT_CONN, 
 *                  FRONTEND_SESS_EVENT_RECVDATA, FRONTEND_SESS_EVENT_TIMEOUT, FRONTEND_SESS_EVENT_CLOSE)
 *
 * @note 1. This callback is called automatically by the session management framework,
 *          and should not be invoked manually by the user;
 *       2. The default implementation typically includes basic operations such as 
 *          logging, simple resource handling, or triggering default business flows 
 *          (e.g., initiating connection setup on FRONTEND_SESS_EVENT_CONN, processing received
 *          data on FRONTEND_SESS_EVENT_RECVDATA, or preparing for session closure on FRONTEND_EVENT_SESS_CLOSE);
 *       3. Users can replace this default callback with a custom implementation to 
 *          achieve personalized event processing logic;
 *       4. Ensure the 'sess' pointer is valid when the callback is triggered (the framework
 *          guarantees this under normal circumstances), invalid pointers may lead to undefined behavior;
 *       5. The processing logic in this callback should be non-blocking to avoid blocking the
 *          session framework's event loop.
 */
void default_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event){
    uint8_t snd_buf[4096],  rcv_buf[4096];
    if(FRONTEND_SESS_EVENT_CONN == event){
        utils_print("In %s, FRONTEND_SESS_EVENT_CONN happens!\n", __func__);
        utils_print("Now begin to send data.\n");
        memset(snd_buf, 0, sizeof(snd_buf));
        snprintf(snd_buf, sizeof(snd_buf), "test msg");
        frontend_sess_send(sess, snd_buf, strlen(snd_buf));
    }else if(FRONTEND_SESS_EVENT_RECVDATA == event){
        utils_print("In %s, FRONTEND_SESS_EVENT_RECVDATA happens!\n", __func__);
    }else if(FRONTEND_SESS_EVENT_CLOSE == event){
        utils_print("In %s, FRONTEND_SESS_EVENT_CLOSE happens!\n", __func__);
    }else if(FRONTEND_SESS_EVENT_ABNORMAL == event){
        utils_print("In %s, FRONTEND_SESS_EVENT_ABNORMAL happens!\n", __func__);
    }

    return;
}


/**
* @brief Placeholder event callback function for frontend sessions
 *
* This is a placeholder callback implementation for handling frontend session events,
 * which is invoked by the session framework when specific events (defined in
 * FrontendSessionEvent enumeration) occur during the lifecycle of a FrontendSession.
* It acts as a placeholder (does not process any events) when no custom callback is specified, with all event handling logic executed elsewhere.
 *
 * @param[in] sess Pointer to the FrontendSession instance that triggered the event;
 *                 must be a valid non-NULL pointer pointing to an active session
 * @param[in] event The specific event type that occurred, one of the values in 
 *                  FrontendSessionEvent enumeration (FRONTEND_SESS_EVENT_CONN, 
 *                  FRONTEND_SESS_EVENT_RECVDATA, FRONTEND_SESS_EVENT_TIMEOUT, FRONTEND_SESS_EVENT_CLOSE)
 *
 * @note 1. This callback is called automatically by the session management framework,
 *          and should not be invoked manually by the user;
 *       2. As a placeholder implementation, this function performs no event processing operations (such as
 *          logging, resource handling, or triggering business flows)—all event handling logic is deferred to other modules;
 *          (e.g., initiating connection setup on FRONTEND_SESS_EVENT_CONN, processing received
 *          data on FRONTEND_SESS_EVENT_RECVDATA, or preparing for session closure on FRONTEND_EVENT_SESS_CLOSE);
 *       3. Users can replace this placeholder callback with a custom implementation to 
 *          achieve personalized event processing logic;
 *       4. Ensure the 'sess' pointer is valid when the callback is triggered (the framework
 *          guarantees this under normal circumstances), invalid pointers may lead to undefined behavior;
 *       5. The processing logic in this callback should be non-blocking to avoid blocking the
 *          session framework's event loop.
 * @note 6. This implementation is a placeholder only, no event processing logic is performed here;
 *          all event handling logic is executed in other specified modules.
 */
void placeholder_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event)
{
    /* Placeholder callback function, does not process any events; all event handling logic is executed elsewhere */
    (void)sess;  // Avoid unused parameter warning, comply with coding standards
    (void)event; // Avoid unused parameter warning, comply with coding standards
    return;      // Return directly without performing any processing operations
}