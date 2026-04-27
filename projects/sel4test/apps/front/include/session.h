#ifndef SESSION_H
#define SESSION_H

#include <stdint.h> 
#include <queue.h>
#include "uthash.h"
// #include "shared_mem_io.h"

// Frontend proxy processing result: Success (data read/written normally, logic executed completely)
#define FRONTEND_PROXY_PROCESS_OK               0

// Frontend proxy processing result: Failure (system-level error, such as memory allocation failure, invalid handle, protocol parsing error, etc. Requires error investigation)
#define FRONTEND_PROXY_PROCESS_ERROR            -1

// Frontend proxy processing result: Process temporarily unavailable (non-error state, only data read/write cannot be completed, such as empty queue, data not ready, resource temporarily occupied, etc. Retry is allowed)
#define FRONTEND_PROXY_PROCESS_AGAIN            1


/**
/**
 * @brief Frontend session related event types
 *
 * This enumeration defines all event types involved in the lifecycle, data interaction,
 * and exception handling of the frontend session (FrontendSession). Each event corresponds
 * to a specific stage or state change of the session.
 */
typedef enum {
    FRONTEND_SESS_EVENT_CONN,        /**< Frontend session connection established event (triggered when the session successfully connects to the peer/backend) */
    FRONTEND_SESS_EVENT_RECVDATA,    /**< Frontend session data received event (triggered when the session receives data from the peer/backend, and the data is ready for processing) */
    FRONTEND_SESS_EVENT_CLOSE,       /**< Frontend session close event (triggered when the session needs to be closed actively or passively, e.g., after receiving close command or peer disconnect) */
    FRONTEND_SESS_EVENT_TIMEOUT,     /**< Frontend session timeout event (triggered when the session has no data interaction or response within the predefined timeout period) */
    FRONTEND_SESS_EVENT_ABNORMAL,    /**< Frontend session abnormal event (triggered when unexpected exceptions occur during session operation, such as resource allocation failure, protocol parsing error, or unexpected connection interruption) */
    FRONTEND_SESS_EVENT_MAX          /**< Total number of frontend session event types (for boundary checking and iteration, not a valid event type) */
} FrontendSessionEvent;



struct ControlMsg{
    uint16_t dev_id;
};

/**
 * @brief Memory source type of the data field in message segment (SessMsgSeg)
 * 
 * Used to identify whether the memory pointed to by the data pointer in the SessMsgSeg structure
 * is dynamically allocated or comes from shared memory (to determine subsequent memory management 
 * approaches, such as whether manual release is required)
 */
typedef enum {
    SESS_MSG_SEG_DYNAMIC_ALLOC,  ///< data points to dynamically allocated memory (needs to be freed with free())
    SESS_MSG_SEG_SHARED_MEM      ///< data points to shared memory (no manual release needed; managed by the shared memory manager)
} SessMsgSegType;


/**
 * @brief Structure representing a segment of session message data, supporting both dynamic and shared memory
 * This structure encapsulates a segment of message data for session communication, including
 * metadata about the data buffer and the buffer itself. It can manage data in two modes:
 * dynamically allocated memory or shared memory (via a shared memory pool).
 */
struct SessMsgSeg {
    /**
     * @brief Length of the currently valid (unread) data in bytes.
     * 
     * This value decreases as data is read from the segment.
     * Initially set to the total size of the received data block.
     * When frontend_sess_recv reads part of this segment, 'len' is reduced
     * and 'data' pointer is advanced accordingly.
     */
    uint16_t len; 

    /**
     * @brief Offset of the current data pointer relative to the original buffer start.
     * 
     * Indicates how many bytes have been consumed from the original allocation/shared memory block.
     * - Initially 0 when the segment is created.
     * - Increases as 'data' pointer advances during partial reads (e.g., in frontend_sess_recv).
     * - Useful for debugging, tracing data progress, or calculating absolute positions in shared memory.
     * 
     * Relationship: Original_Data_Start + offset == Current data pointer (if base address is known).
     */
    uint16_t offset; 

    /**
     * @brief Memory source type of the data buffer, corresponding to SessMsgSegType.
     * 
     * Valid values:
     * - SESS_MSG_SEG_DYNAMIC_ALLOC: Data points to malloc()'ed memory. 'mem_pool' should be NULL.
     * - SESS_MSG_SEG_SHARED_MEM: Data points to a region within a shared memory pool. 'mem_pool' must be valid.
     */
    uint16_t type; 

    /**
     * @brief Pointer to the associated shared memory pool.
     * 
     * - Valid (non-NULL) ONLY when @ref type is SESS_MSG_SEG_SHARED_MEM.
     *   Used to manage ownership, reference counting, and proper release of shared memory.
     * - NULL when @ref type is SESS_MSG_SEG_DYNAMIC_ALLOC.
     */
    struct SharedMemoryPool *mem_pool;

    /**
     * @brief Pointer to the current start of the unread data buffer.
     * 
     * - For SESS_MSG_SEG_DYNAMIC_ALLOC: Points to the current position in dynamically allocated memory.
     *   Initially points to malloc() return value; advances as data is consumed.
     * - For SESS_MSG_SEG_SHARED_MEM: Points to the current valid data segment within the shared memory.
     * 
     * @note This pointer is MUTABLE. Functions like frontend_sess_recv will increment this pointer
     *       and decrease @ref len when only part of the segment is read, preserving the remaining data.
     */ 
    uint8_t *data; 

    /**
     * @brief TAILQ queue entry.
     * 
     * Links this segment into the session's receive queue (msg_b2f), allowing sequential traversal
     * and aggregation of multiple message fragments.
     */
    TAILQ_ENTRY(SessMsgSeg) entry; 
};


TAILQ_HEAD(SessMsgQueue, SessMsgSeg);



struct SessMsgSeg *sess_msg_seg_alloc(size_t len, SessMsgSegType type, uint8_t *shared_data, struct SharedMemoryPool *mem_pool);
struct SessMsgSeg* sess_msg_seg_alloc_lite(SessMsgSegType type);
void sess_msg_seg_free(struct SessMsgSeg *seg_ptr);

void sess_msg_queue_free_all(struct SessMsgQueue *queue);

struct FrontendProtocolProcess; 
struct FrontendEngine_;
struct FrontendSession;

#define FRONTEND_SESS_LINKED_TO_QUEUE            1
typedef void (*SESS_CALLBACK)(struct FrontendSession *sess, uint8_t *data, int len);
typedef void (*SESS_EVENT_CALLBACK)(struct FrontendSession *sess, FrontendSessionEvent event);



/**
 * @brief Frontend session lifecycle state enumeration
 *
 * Defines all possible lifecycle states of a frontend session, describing the connection status
 * between the frontend and backend during the session's lifetime.
 */
typedef enum {
    FRONTEND_SESS_INITIALIZE,  ///< Initial state: Newly created frontend session (no connection attempt yet)
    FRONTEND_SESS_CONNECTING,  ///< Connecting state: Attempting to establish a connection with the backend
    FRONTEND_SESS_CONNECTED,   ///< Connected state: Successfully established connection with the backend session
    FRONTEND_SESS_CLOSED       ///< Closed state: Session has been terminated (final state)
} FrontendSessState;


struct FrontendSession {
    int                             sess_type;
/*
 * Session overall state machine, identifying the lifecycle phase of the frontend session.
 * Supported states:
 *      1. FRONTEND_SESS_INITIALIZE: Newly created frontend session (initial state)
 *      2. FRONTEND_SESS_CONNECTING: Attempting to establish a connection with the backend
 *      3. FRONTEND_SESS_CONNECTED: Connection with the backend session has been successfully established
 *      4. FRONTEND_SESS_CLOSED: Session has been closed (terminated state)
 */
    FrontendSessState               sess_state;
    int                             ip_version;
    uint16_t                        frontend_sess_id;
    uint16_t                        backend_sess_id; // hash key
/*
 * State machine states, indicating the linked status of the session in different directions
 * state_f2b: When its value is FRONTEND_SESS_LINKED_TO_QUEUE, it means the entries_f2b node of the current session
 * is linked to the queue_f2b (belonging to FrontendSessionPool in FrontendEngine_)
 */
    uint8_t                         state_f2b; 
    uint8_t                         state_b2f; 
    
// Message queues
    struct SessMsgQueue             msg_f2b; // front-end to back-end message queue
    struct SessMsgQueue             msg_b2f; // back-end to front-end message queue
// Queue link nodes
    TAILQ_ENTRY(FrontendSession)    entries_f2b; // front-end to back-end active queue node
    TAILQ_ENTRY(FrontendSession)    entries_b2f; // back-end to front-end active queue node
// Protocol processing
    struct FrontendProtocolProcess  *protocol_process; // protocol processing module pointer
// Pointer to the Frontend engine associated with this session
    struct FrontendEngine_          *eng;
// Private data pointer (used to store session-specific data)
    void                            *pri_data;
    SESS_CALLBACK                   data_process_callback;
    SESS_EVENT_CALLBACK             event_callback;
    UT_hash_handle                  hh;
};


/**
 * @brief Get the block size of the front-end session's queue in the specified direction (with null-pointer safe check, branchless concatenation implementation)
 * 
 * Dynamically select the queue member using identifier concatenation without conditional judgment; 
 * perform step-by-step null-pointer checks along the pointer chain to avoid illegal access.
 * Pointer chain: sess -> eng -> [tx_queue/rx_queue] -> block_size (target queue member generated by concatenating dir and _queue via ##)
 * 
 * @param sess Pointer to the front-end session instance (type: struct FrontendSession *), which can be NULL
 * @param dir Queue direction identifier, only two valid values are supported: tx (corresponds to tx_queue) and rx (corresponds to rx_queue)
 * 
 * @return uint16_t Block size of the queue (block_size):
 *         - Non-0: All dependent pointers are valid, returns the actual configured block size (unit: bytes)
 *         - 0: Any dependent pointer is NULL (sess/eng/target direction queue) or no corresponding member after dir concatenation (may report an error in advance during compilation)
 * 
 * @note 1. Branchless design: Directly generate tx_queue/rx_queue member names by concatenating dir and _queue via ##, 
 *            completely replacing the original conditional judgment with higher execution efficiency and simpler syntax;
 *       2. Compile-time validity check: If an invalid value other than tx/rx is passed to dir (e.g., txx), 
 *            concatenation will generate a non-existent member like txx_queue, which directly reports an error during compilation,
 *            avoiding issues earlier than runtime judgment;
 *       3. Null-pointer safety retained: Maintains the original three-level pointer check (sess→eng→queue), 
 *            and directly returns 0 when any link is NULL through the && short-circuit feature, without sacrificing null-pointer safety;
 *       4. Type compatibility: The return type is consistent with block_size (uint16_t), and 0U is an unsigned integer 0,
 *            compatible with numerical comparison scenarios;
 *       5. Concatenation safety: All parameters and member accesses in the macro are enclosed in parentheses to avoid 
 *            syntax errors caused by operator precedence conflicts.
 */
#define SESS_GET_QUEUE_BLOCK_SIZE(sess, dir) \
( \
    (sess) != NULL && \
    (sess)->eng != NULL && \
    (sess)->eng->dir##_queue != NULL \
    ? (sess)->eng->dir##_queue->block_size \
    : 0U \
)


/**
 * @brief Get the block size of the front-end session's hyper queue in the specified direction (with null-pointer safe check, branchless concatenation implementation)
 * 
 * Dynamically select the hyper queue member using identifier concatenation without conditional judgment; 
 * perform step-by-step null-pointer checks along the pointer chain to avoid illegal access.
 * Pointer chain: sess -> eng -> [hyper_tx_queue/hyper_rx_queue] -> block_size (target hyper queue member generated by concatenating hyper_, dir and _queue via ##)
 * 
 * @param sess Pointer to the front-end session instance (type: struct FrontendSession *), which can be NULL
 * @param dir Queue direction identifier, only two valid values are supported: tx (corresponds to hyper_tx_queue) and rx (corresponds to hyper_rx_queue)
 * 
 * @return uint16_t Block size of the hyper queue (block_size):
 *         - Non-0: All dependent pointers are valid, returns the actual configured block size (unit: bytes)
 *         - 0: Any dependent pointer is NULL (sess/eng/target direction hyper queue) or no corresponding member after dir concatenation (may report an error in advance during compilation)
 * 
 * @note 1. Branchless design: Directly generate hyper_tx_queue/hyper_rx_queue member names by concatenating hyper_, dir and _queue via ##, 
 *            completely replacing the original conditional judgment with higher execution efficiency and simpler syntax;
 *       2. Compile-time validity check: If an invalid value other than tx/rx is passed to dir (e.g., txx), 
 *            concatenation will generate a non-existent member like hyper_txx_queue, which directly reports an error during compilation,
 *            avoiding issues earlier than runtime judgment;
 *       3. Null-pointer safety retained: Maintains the original three-level pointer check (sess→eng→hyper queue), 
 *            and directly returns 0 when any link is NULL through the && short-circuit feature, without sacrificing null-pointer safety;
 *       4. Type compatibility: The return type is consistent with block_size (uint16_t), and 0U is an unsigned integer 0,
 *            compatible with numerical comparison scenarios;
 *       5. Concatenation safety: All parameters and member accesses in the macro are enclosed in parentheses to avoid 
 *            syntax errors caused by operator precedence conflicts.
 */
#define SESS_GET_HYPER_QUEUE_BLOCK_SIZE(sess, dir) \
( \
    (sess) != NULL && \
    (sess)->eng != NULL && \
    (sess)->eng->hyper_##dir##_queue != NULL \
    ? (sess)->eng->hyper_##dir##_queue->block_size \
    : 0U \
)


/**
 * @brief Link Frontend session to the specified queue (only if not linked yet) and set state bit
 * 
 * @param[in]  sess  Pointer to struct FrontendSession (target session)
 * @param[in]  dir   Direction identifier ("f2b" for front2back, "b2f" for back2front)
 * 
 * @details 1. Validate critical pointers (sess/eng/sess_pool)
 *          2. Check if FRONTEND_SESS_LINKED_TO_QUEUE bit is NOT set in state_<dir>
 *          3. If not set: set the bit (bitwise OR) + insert entries_<dir> into queue_<dir>
 * 
 * @note - Avoids duplicate linking (prevents inserting the same node into TAILQ multiple times)
 *       - FRONTEND_SESS_LINKED_TO_QUEUE must be a single-bit mask (e.g., 1U<<0)
 */
#define FRONTEND_SESS_LINK_TO_QUEUE(sess, dir) do {                          \
    /* Step 1: Validate pointers to avoid null dereference */                \
    if ((sess) != NULL && (sess)->eng != NULL && (sess)->eng->sess_pool != NULL) { \
        /* Step 2: Check if NOT linked yet (target bit is 0) */              \
        if (((sess)->state_##dir & FRONTEND_SESS_LINKED_TO_QUEUE) == 0) {      \
            /* Step 3: Set linked bit + insert into queue */                 \
            (sess)->state_##dir |= FRONTEND_SESS_LINKED_TO_QUEUE;              \
            TAILQ_INSERT_TAIL(&(sess)->eng->sess_pool->queue_##dir, (sess), entries_##dir); \
        }                                                                     \
    }                                                                        \
} while (0)



/**
 * @brief Unlink Frontend session from the specified queue (only if linked) and clear state bit
 * 
 * @param[in]  sess  Pointer to struct FrontendSession (target session)
 * @param[in]  dir   Direction identifier ("f2b" for front2back, "b2f" for back2front)
 * 
 * @details 1. Validate critical pointers (sess/eng/session_pool)
 *          2. Check if FRONTEND_SESS_LINKED_TO_QUEUE bit is set in state_<dir>
 *          3. If set: remove entries_<dir> from queue_<dir> + clear the bit (bitwise AND NOT)
 * 
 * @note - Avoids invalid unlinking (prevents removing a node not in TAILQ)
 *       - Ensure queue_<dir> is initialized before calling
 */
#define FRONTEND_SESS_UNLINK_FROM_QUEUE(sess, dir) do {                      \
    /* Step 1: Validate pointers to avoid null dereference */                \
    if ((sess) != NULL && (sess)->eng != NULL && (sess)->eng->sess_pool != NULL) { \
        /* Step 2: Check if already linked (target bit is 1) */              \
        if (((sess)->state_##dir & FRONTEND_SESS_LINKED_TO_QUEUE) != 0) {      \
            /* Step 3: Remove from queue + clear linked bit */                \
            TAILQ_REMOVE(&(sess)->eng->sess_pool->queue_##dir, (sess), entries_##dir); \
            (sess)->state_##dir &= ~FRONTEND_SESS_LINKED_TO_QUEUE;             \
        }                                                                     \
    }                                                                        \
} while (0)


/**
 * @brief Get the associated shared memory pool (mem_pool) from a FrontendSession pointer
 * This macro retrieves the shared_memory_pool pointer contained in FrontendEngine_
 * through the eng member (pointing to FrontendEngine_) of the FrontendSession structure.
 * @param sess Pointer to a struct FrontendSession
 * @return Pointer to struct SharedMemoryPool, i.e., sess->eng->mem_pool
 */
#define FRONTEND_SESSION_MEM_POOL(sess) ((sess)->eng->mem_pool)


/**
 * @brief Insert a SessMsgSeg pointer into the specified direction queue of FrontendSession
 * 
 * @param[in]  sess  Pointer to struct FrontendSession (the session containing the target queue)
 * @param[in]  seg   Pointer to struct SessMsgSeg (the message segment to be inserted)
 * @param[in]  dir   Direction identifier, must be "f2b" (front2back) or "b2f" (back2front)
 * 
 * @details 1. Validate that sess and seg are non-NULL to avoid null dereference
 *          2. Insert seg into sess->msg_<dir> queue using TAILQ_INSERT_TAIL (FIFO order)
 *          3. The queue is identified by concatenating "msg_" with dir (msg_f2b or msg_b2f)
 * 
 * @note - <dir> must be "f2b" or "b2f"; invalid values will cause compilation errors
 *       - Ensure sess->msg_<dir> has been initialized with TAILQ_INIT() before insertion
 *       - seg must point to a valid SessMsgSeg instance (allocated and initialized)
 *       - This macro performs a tail insertion to maintain FIFO order of messages
 */
#define SESS_MSG_SEG_INSERT_QUEUE(sess, seg, dir) do {                     \
    /* Validate critical pointers */                                        \
    if ((sess) != NULL && (seg) != NULL) {                                  \
        /* Insert the segment into the target queue (msg_f2b or msg_b2f) */ \
        TAILQ_INSERT_TAIL(&(sess)->msg_##dir, (seg), entry);                \
    }                                                                        \
} while (0)


/**
 * @brief Remove and return the first SessMsgSeg pointer from the specified direction queue of FrontendSession
 * 
 * @param[in]  sess     Pointer to struct FrontendSession (the session containing the target queue)
 * @param[out] seg_ptr  Double pointer to struct SessMsgSeg (output: receives the removed segment; set to NULL if queue is empty)
 * @param[in]  dir      Direction identifier, must be "f2b" (front2back) or "b2f" (back2front)
 * 
 * @details 1. Validate that sess and seg_ptr are non-NULL to avoid null dereference
 *          2. Check if the target queue (msg_<dir>) is non-empty using TAILQ_FIRST
 *          3. If non-empty: remove the first element with TAILQ_REMOVE and assign to *seg_ptr
 *          4. If empty: set *seg_ptr to NULL
 * 
 * @note - <dir> must be "f2b" or "b2f"; invalid values will cause compilation errors
 *       - Ensure sess->msg_<dir> has been initialized with TAILQ_INIT() before removal
 *       - seg_ptr must be a valid double pointer (points to a struct SessMsgSeg* variable)
 *       - The removed segment's memory is not freed by this macro (caller must handle via sess_msg_seg_free)
 */
#define SESS_MSG_SEG_REMOVE_HEAD(sess, seg_ptr, dir) do {                  \
    /* Validate critical pointers */                                        \
    if ((sess) != NULL && (seg_ptr) != NULL) {                              \
        /* Initialize output to NULL (handles empty queue case) */           \
        *(seg_ptr) = NULL;                                                  \
        /* Check if queue is non-empty */                                    \
        if (TAILQ_FIRST(&(sess)->msg_##dir) != NULL) {                      \
            /* Get the first element and remove it from the queue */         \
            *(seg_ptr) = TAILQ_FIRST(&(sess)->msg_##dir);                   \
            TAILQ_REMOVE(&(sess)->msg_##dir, *(seg_ptr), entry);             \
        }                                                                    \
    }                                                                        \
} while (0)

struct FrontendProtocolProcess {

    int (*connect)(struct FrontendSession* sess);
    
    int (*accept)(struct FrontendSession* sess);
    
    int (*read)(struct FrontendSession* sess, uint8_t* data, uint32_t size);
    
    int (*write)(struct FrontendSession* sess, const uint8_t* data, uint32_t size);
    
    int (*close)(struct FrontendSession* sess);

};

TAILQ_HEAD(FrontendSessionQueue, FrontendSession);

struct FrontendSessionID {
    uint16_t id;
    TAILQ_ENTRY(FrontendSessionID) entry;
};
TAILQ_HEAD(FrontendSessionIDQueue, FrontendSessionID);


void default_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event);
void placeholder_session_event_callback(struct FrontendSession* sess, FrontendSessionEvent event);
int session_send(struct FrontendSession* sess, const uint8_t* data, uint32_t size);
int session_recv(struct FrontendSession* sess, uint8_t* data, uint32_t size);

/**
 * @brief Macro to convert a dotted-decimal IPv4 string to an IPv4Address structure
 * @details Parses a string in "xxx.xxx.xxx.xxx" format, validates each octet range (0-255),
 *          and populates the IPv4Address structure with the binary representation.
 *          Provides error messages to stderr for invalid formats or out-of-range values.
 * 
 * @param ip_str Input string in dotted-decimal IPv4 format (e.g., "192.168.1.1")
 * @param addr_struct Output IPv4Address structure to be populated with parsed values
 */
#define IPV4_STR_TO_ADDR(ip_str, addr_struct) do { \
    unsigned int a, b, c, d;  /**< Temporary storage for parsed octet values */ \
    \
    /* Attempt to parse 4 octets from the input string */ \
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) { \
        \
        /* Validate all octets are within the 0-255 range */ \
        if (a <= 255 && b <= 255 && c <= 255 && d <= 255) { \
            /* Populate the structure with validated octets */ \
            (addr_struct).data[0] = (uint8_t)a; \
            (addr_struct).data[1] = (uint8_t)b; \
            (addr_struct).data[2] = (uint8_t)c; \
            (addr_struct).data[3] = (uint8_t)d; \
        } else { \
            /* Handle octet values outside valid range */ \
            error_print("IPV4_STR_TO_ADDR failed: Invalid IPv4 address!"); \
        } \
    } else { \
        /* Handle invalid string format (not matching xxx.xxx.xxx.xxx) */ \
        error_print("IPV4_STR_TO_ADDR failed: Invalid IPv4 format!"); \
    } \
} while(0)



/**
 * @brief Macro to convert an IPv4:port string to an IPv4PortTuple structure
 * @details Parses a string in "xxx.xxx.xxx.xxx:port" format, splits it into IP address
 *          and port components, validates both parts, and populates the IPv4PortTuple.
 *          Port numbers must be in the range 0-65535.
 * 
 * @param ip_port_str Input string in "xxx.xxx.xxx.xxx:port" format (e.g., "192.168.1.100:8080")
 * @param tuple_struct Output IPv4PortTuple structure to be populated with parsed values
 */
#define IPV4_PORT_STR_TO_TUPLE(ip_port_str, tuple_struct) do { \
    char ip_str[16];  /* Buffer to store the IP address part (max IPv4 string length is 15) */ \
    unsigned int port; \
    \
    /* Parse the IP address and port from the input string */ \
    if (sscanf(ip_port_str, "%15[^:]:%u", ip_str, &port) == 2) { \
        \
        /* Convert and validate the IP address part */ \
        IPV4_STR_TO_ADDR(ip_str, (tuple_struct).ipv4_addr); \
        \
        /* Validate the port number (0-65535 range) */ \
        if (port > 65535) { \
            error_print("IPV4_PORT_STR_TO_TUPLE failed: invalid port number: must be 0-65535)!"); \
        } else { \
            (tuple_struct).port = (uint16_t)port; \
        } \
    } else { \
        error_print("IPV4_PORT_STR_TO_TUPLE failed: invalid IP:port format!"); \
    } \
} while(0)



/**
 * @brief IoT Session related event types
 *
 * This enumeration defines all event types involved in the lifecycle, command interaction,
 * data reporting, and exception handling of an IoT device session (IoTSession).
 * It is tailored for scenarios involving device registration, remote control, and telemetry.
 */
typedef enum {
    /* --- Lifecycle Events --- */
    
    /** 
     * @brief IoT session registration/login successful. 
     * Triggered when the device completes authentication and is officially online in the system.
     * Distinct from physical connection; implies logical business availability.
     */
    IOT_SESS_EVENT_REGISTERED,      

    /** 
     * @brief IoT session logout/unregistered. 
     * Triggered when the device actively logs out or is kicked offline by the server (e.g., duplicate login).
     */
    IOT_SESS_EVENT_UNREGISTERED,    

    /** 
     * @brief Physical connection lost. 
     * Triggered when the underlying transport (TCP/MQTT/CoAP/BLE) disconnects unexpectedly.
     * The session may attempt reconnection depending on policy.
     */
    IOT_SESS_EVENT_DISCONNECTED,    

    /* --- Data & Command Interaction Events --- */

    /** 
     * @brief Downlink command received. 
     * Triggered when the session receives a control instruction from the cloud/app (e.g., "Turn On Light", "Set Temp").
     * Payload requires parsing and execution.
     */
    IOT_SESS_EVENT_CMD_RECEIVED,    

    /** 
     * @brief Downlink data received. 
     * Triggered when the session receives a data message from the cloud/app.
     */
    IOT_SESS_EVENT_DATA_RECEIVED,    

    /** 
     * @brief Uplink telemetry/report received. 
     * Triggered when the session receives periodic status data or sensor readings from the device (e.g., Temperature, GPS).
     * Payload is typically forwarded to storage or analysis modules.
     */
    IOT_SESS_EVENT_REPORT_RECEIVED, 

    /** 
     * @brief Command execution response received. 
     * Triggered when the device replies with the result of a previously sent command (Success/Failure/Timeout).
     */
    IOT_SESS_EVENT_CMD_RESPONSE,    

    /** 
     * @brief Heartbeat/Ping received. 
     * Triggered when a keep-alive packet is received, used to reset the session timeout timer.
     */
    IOT_SESS_EVENT_HEARTBEAT,       

    /** 
     * @brief Session state synchronization required. 
     * Triggered when the device reconnects after a dropout and needs to sync its latest state with the server.
     */
    IOT_SESS_EVENT_SYNC_REQUIRED,   

    /* --- Exception & Error Events  --- */

    /** 
     * @brief Session timeout. 
     * Triggered when no heartbeat or data is received within the predefined threshold (Device might be asleep or dead).
     */
    IOT_SESS_EVENT_TIMEOUT,         

    /** 
     * @brief Authentication failure. 
     * Triggered when the device provides invalid credentials, token expired, or unauthorized access attempt.
     */
    IOT_SESS_EVENT_AUTH_FAILED,     

    /** 
     * @brief General abnormal error. 
     * Triggered for unexpected issues: protocol parsing errors, memory allocation failure, or invalid payload format.
     */
    IOT_SESS_EVENT_ABNORMAL,        

    /** 
     * @brief Device resource warning (IoT Specific). 
     * Triggered when the device reports low battery, weak signal, or storage full (if conveyed via session metadata).
     */
    IOT_SESS_EVENT_RESOURCE_WARNING,

    /* --- Boundary --- */
    
    /** 
     * @brief Total number of IoT session event types. 
     * Used for array sizing and boundary checking. Not a valid event type.
     */
    IOT_SESS_EVENT_MAX              
} IoTSessionEvent;



/**
 * @brief IoT session link state enumeration
 */
typedef enum {
    IOT_SESS_STATE_UNINIT = 0,    // Session uninitialized
    IOT_SESS_STATE_LINKED,        // Session linked to IoT device (active)
    IOT_SESS_STATE_UNLINKED,      // Session unlinked from device (inactive)
    IOT_SESS_STATE_ERROR          // Session in error state (need reconnection)
} IotSessLinkState;


/**
 * @brief Socket node for managing multiple file descriptors within a single session.
 * 
 * Some IoT sessions (e.g., Bluetooth Gateway, Modbus TCP Server) may involve 
 * multiple sockets: one listening socket, one or more connected client sockets, 
 * or separate control/data channels. This struct allows tracking them in a list.
 */
typedef struct IotSockNode_ {
    int                     fd;                  // The file descriptor of the socket
    TAILQ_ENTRY(IotSockNode_) entries;           // Linkage member for the TAILQ
} IotSockNode;

// Define the TAILQ head type for the list of IotSockNode
TAILQ_HEAD(IotSockList_, IotSockNode_);
typedef struct IotSockList_ IotSockList;


/**
 * @brief Working mode of an IoT session/device.
 * 
 * Defines the operational role of the entity in the IoT architecture.
 * This dictates the connection behavior (initiator vs listener) and data flow direction.
 * 
 * - CLIENT_MODE: The device initiates connections and reports data (e.g., Sensors, Actuators).
 * - SERVER_MODE: The device listens for connections and manages clients (e.g., Gateways, Hubs).
 */
typedef enum IotWorkMode_ {
    /** 
     * @brief Client Mode (Initiator / Edge Device).
     * 
     * Characteristics:
     * - Actively initiates connections to a server/gateway.
     * - Primary data flow: Uplink (Device -> Server).
     * - Responsible for auto-reconnection on link failure.
     * - Typical devices: Temperature sensors, Smart plugs, Wearables.
     * - Network behavior: Uses connect(), no listen_fd.
     */
    IOT_WORK_MODE_CLIENT = 0,

    /** 
     * @brief Server Mode (Listener / Gateway / Hub).
     * 
     * Characteristics:
     * - Passively waits for incoming connections from clients.
     * - Primary data flow: Downlink management & Uplink aggregation.
     * - Responsible for accepting new clients and managing multiple sessions.
     * - Typical devices: IoT Gateways, Edge Servers, Modbus TCP Masters (acting as servers).
     * - Network behavior: Uses bind() + listen() + accept(), owns listen_fd.
     */
    IOT_WORK_MODE_SERVER = 1
} IotWorkMode;


/**
 * @brief IoT message queue structure (optimized for IoT small data packets)
 * @note Simplified version of SessMsgQueue for IoT characteristics
 */
typedef struct IotSessMsgQueue_ {
    void                *msg_buf;    // Message buffer (fixed size for IoT)
    uint32_t            buf_size;    // Buffer size (e.g., 4096 bytes)
    uint32_t            msg_count;   // Number of pending messages
    int                 queue_fd;    // Event fd for queue notification
} IotSessMsgQueue;


typedef struct IotMsgBuffer_;
typedef struct IotMsgBuffer_ IotMsgBuffer;
struct IoTFrontendSession_;

typedef void (*IOTSESS_EVENT_CALLBACK)(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);


/**
 * @brief IoT frontend session object (for Bluetooth/CAN/Zigbee/LoRa/OpenPowerLink/ModbusTCP)
 * @note Each IoT device has exactly one corresponding session (1:1 mapping)
 * @note No session pool required - directly bound to IoT device instance
 */
typedef struct IoTFrontendSession_ {
    // 1. Session core identification (1:1 binding with IoT device)
    int                 sess_type;           // IoT session type (matches protocol type)
    int                 sess_role;
    int                 dev_id;              // Associated IoT device ID (global unique)
    int                 sess_id;             // IoT session ID (same as device's sess_id)
    IotWorkMode         working_mode;        // Session working mode (client/gateway)
    
    // 2. Link state management (simplified for IoT 1:1 mapping)
    IotSessLinkState    sess_dev_link_state; // Session-device link state
    uint64_t            last_link_ts;        // Last link state change timestamp (ms)
    uint32_t            reconnect_count;     // Reconnection attempt count (for error recovery)
    
    // 3. Device association (direct pointer to IoT device object)
    struct IotDevice_       *bound_dev;          // Pointer to bound IoT device (core association)
    struct FrontendEngine_  *eng;              // Pointer to parent backend engine
    
    // 4. Message queues (optimized for IoT small packets)
    IotSessMsgQueue     msg_dev2eng;         // Device to engine message queue (data report/notify)
    IotSessMsgQueue     msg_eng2dev;         // Engine to device message queue (control command)
    
    // 5. Protocol processing (IoT protocol-specific handler)
    void                *proto_handler;      // Protocol-specific processing module (e.g., BLE handler)
    void                *pri_data;           // Private data (protocol-specific context)
    
    // 6. Hardware/IO related (IoT device access)
    int                 dev_fd;              // Bound device file descriptor (same as device's fd)
    int                 listen_fd;           // Listening socket for connection-oriented protocols (e.g., Bluetooth L2CAP, Modbus TCP). 
    IotSockList         sock_list;           // Head of the linked list for socket nodes
    uint32_t            io_timeout;          // IO operation timeout (ms, e.g., 5000)
    
    // 7. Statistics (session-level performance metrics)
    uint64_t            tx_packets;          // Total packets sent via session
    uint64_t            rx_packets;          // Total packets received via session
    uint64_t            tx_bytes;            // Total bytes sent
    uint64_t            rx_bytes;            // Total bytes received
    uint32_t            error_count;         // Total session errors
        /**
     * @brief Send data to backend (protocol-agnostic)
     * @param sess Pointer to IoTBackendSession instance
     * @param msg_buf Pointer to IotMsgBuffer (contains data + destination address)
     * @return int BACKEND_PROXY_PROCESS_OK on success, BACKEND_PROXY_PROCESS_ERROR on failure
     */
    int (*send_to_backend)(struct IoTFrontendSession_ *sess, const IotMsgBuffer *msg_buf);

    /**
     * @brief Receive data from backend (protocol-agnostic)
     * @param sess Pointer to IoTFrontendSession instance
     * @param msg_buf Pointer to IotMsgBuffer (assembled and returned to caller)
     * @param data Raw data buffer received from backend
     * @param data_len Length of raw data received from backend
     * @return int FRONTEND_PROXY_PROCESS_OK on success, FRONTEND_PROXY_PROCESS_ERROR on failure
     */
    int (*recv_from_backend)(struct IoTFrontendSession_ *sess, 
                             IotMsgBuffer *msg_buf,
                             uint8_t *data,
                             uint16_t data_len);

    IOTSESS_EVENT_CALLBACK event_callback;
    // 8. List linkage (for engine-level session management)
    TAILQ_ENTRY(IoTFrontendSession_) entries; // Linked list node for engine session list
} IoTFrontendSession;

extern IoTFrontendSession *frontend_bluetooth_sess;
extern IoTFrontendSession *frontend_can_sess;
extern IoTFrontendSession *frontend_zigbee_sess;
extern IoTFrontendSession *frontend_lora_sess;
extern IoTFrontendSession *frontend_powerlink_sess;
extern IoTFrontendSession *frontend_modbustcp_sess;



int bluetooth_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int bluetooth_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

int can_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int can_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

int zigbee_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int zigbee_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

int lora_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int lora_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

int powerlink_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int powerlink_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

int modbustcp_send_to_backend(IoTFrontendSession *sess, const IotMsgBuffer *msg_buf);
int modbustcp_recv_from_backend(IoTFrontendSession *sess, IotMsgBuffer *msg_buf, uint8_t *data, uint16_t data_len);

void default_bluetooth_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);
void default_can_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);
void default_zigbee_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);
void default_lora_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);
void default_powerlink_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);
void default_modbustcp_event_callback(struct IoTFrontendSession_ *sess, IoTSessionEvent event, IotMsgBuffer *msg_buf);


/**
 * @brief Get max payload length by frontend session type
 * @param sess IoTFrontendSession* pointer
 * @return Corresponding max payload value, 0 if invalid type
 */
#define FRONTEND_IOCT_SESS_GET_MAX_PAYLOAD(sess) \
({                                              \
    uint16_t __max_pay = 0;                     \
    if ((sess) != NULL) {                       \
        switch ((sess)->sess_type) {            \
            case IOT_PROTO_TYPE_BLUETOOTH:      \
                __max_pay = FRONTEND_BLUETOOTH_MAX_PAYLOAD; break; \
            case IOT_PROTO_TYPE_ZIGBEE:         \
                __max_pay = FRONTEND_ZIGBEE_MAX_PAYLOAD; break;    \
            case IOT_PROTO_TYPE_CAN:            \
                __max_pay = FRONTEND_CAN_MAX_PAYLOAD; break;       \
            case IOT_PROTO_TYPE_LORA:           \
                __max_pay = FRONTEND_LORA_MAX_PAYLOAD; break;      \
            case IOT_PROTO_TYPE_POWERLINK:      \
                __max_pay = FRONTEND_POWERLINK_MAX_PAYLOAD; break; \
            case IOT_PROTO_TYPE_MODBUSTCP:      \
                __max_pay = FRONTEND_MODBUS_TCP_MAX_PAYLOAD; break;\
            default:                            \
                __max_pay = 0; break;           \
        }                                       \
    }                                           \
    __max_pay;                                  \
})

#endif