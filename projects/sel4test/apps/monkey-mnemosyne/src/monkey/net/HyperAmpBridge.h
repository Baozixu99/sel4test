/*
 * HyperAMP Network Bridge – Singleton
 *
 * Proxies POSIX-style connect/send/recv/close over HyperAMP shared-memory
 * queues so that monkey-mnemosyne (running on seL4, which has no network
 * stack) can reach the Linux backend transparently.
 *
 * Design principles
 * -----------------
 *   - Direct use of the HyperAMP queue primitives (hyperamp_queue_enqueue /
 *     hyperamp_queue_dequeue).  No dependency on apps/front beyond the
 *     shared C headers that define the queue and message structures.
 *   - Synchronous / polling semantics: every receive busy-waits until data
 *     arrives.  This is intentional – performance is not a concern for the
 *     monkey protocol's low-bandwidth control traffic.
 *   - One active session at a time (sufficient for monkey-mnemosyne).
 *
 * Created 2025 – Monkey-Mnemosyne / seL4 HyperAMP integration
 *
 * gongty [at] tongji [dot] edu [dot] cn
 */

#pragma once

#include <adl/sys/types.h>
#include <monkey/net/IP4Addr.h>

/* HyperAMP C headers (local copies – see hyperamp_shm_queue.h comment) --- */
extern "C" {
#include "hyperamp_shm_queue.h"
#include "hyperamp_protocol_defs.h"
}

namespace monkey::net {


/**
 * Maximum payload that fits in a single HyperAMP queue slot after
 * subtracting the 8-byte HyperampMsgHeader.
 */
static constexpr adl::size_t kHyperAmpMaxPayload = HYPERAMP_MSG_MAX_SIZE;

/**
 * Combined header + max-payload – equals one queue block.
 */
static constexpr adl::size_t kHyperAmpBlockSize = HYPERAMP_MSG_HDR_PLUS_MAX_SIZE;


/**
 * @brief Singleton bridge that replaces POSIX sockets with HyperAMP
 *        shared-memory queue operations.
 *
 * Typical usage (called from Socket4::connect, PromisedSocketIo::send, etc.):
 *
 *     auto& bridge = HyperAmpBridge::instance();
 *     bridge.init();                          // once, at boot
 *     bridge.connect(ip, port);               // session create
 *     bridge.send(data, len);                 // data transfer
 *     bridge.recv(buf, len);                  // blocking poll
 *     bridge.close();                         // session close
 */
class HyperAmpBridge {
public:
    /* ---- Singleton access ----------------------------------------------- */

    static HyperAmpBridge& instance();

    /* non-copyable, non-movable */
    HyperAmpBridge(const HyperAmpBridge&)            = delete;
    HyperAmpBridge& operator=(const HyperAmpBridge&) = delete;
    HyperAmpBridge(HyperAmpBridge&&)                 = delete;
    HyperAmpBridge& operator=(HyperAmpBridge&&)      = delete;

    /* ---- Lifecycle ------------------------------------------------------ */

    /**
     * Initialise the HyperAMP TX/RX queues — legacy entry point.
     *
     * Reads queue virtual addresses from the compile-time constants in
     * hyperamp_shm_queue.h, the way this bridge originally worked.
     *
     * IMPORTANT: after the multi-channel rework in the kernel boot
     * loader those compile-time vaddrs are no longer guaranteed to
     * match the actual mapping.  This entry point is preserved purely
     * so that the legacy lab-main.cc test path keeps compiling; in
     * production code use the four-argument overload below and pass
     * vaddrs read from seL4_GetMR() instead.
     *
     * @param channelId  1 = ch1 layout (default), 2 = ch2 layout.
     */
    void init(adl::uint8_t channelId = 1);

    /**
     * Initialise the HyperAMP TX/RX queues with explicit virtual
     * addresses obtained from the IPC message registers.
     *
     * This is the modern entry point used by mnemosyne_engine_init().
     * The caller is responsible for reading seL4_GetMR(...) BEFORE
     * issuing any other syscall (the IPC buffer is volatile across
     * syscalls; see apps/hyperamp-server/src/main.c for the exact
     * ordering rule).
     *
     * Idempotent: subsequent calls are no-ops.
     *
     * @param channelId  Channel id, used only for paddr-offset selection
     *                   (1 = CH1, 2 = CH2).  Determines the `phy_addr`
     *                   passed into hyperamp_queue_init() so that the
     *                   underlying mapping table is consistent with what
     *                   the kernel actually mapped.
     * @param txVa       Virtual address of the TX queue control block,
     *                   as published in the IPC MR by the boot loader.
     * @param rxVa       Virtual address of the RX queue control block.
     * @param dataVa     Virtual address of the shared data region.
     */
    void init(adl::uint8_t  channelId,
              adl::uint64_t txVa,
              adl::uint64_t rxVa,
              adl::uint64_t dataVa);

    /**
     * @return true after a successful init().
     */
    bool isInitialized() const { return initialized_; }

    /* ---- Session management --------------------------------------------- */

    /**
     * Create a TCP session via the HyperAMP proxy.
     *
     * Builds a HYPERAMP_MSG_TYPE_SESS / SESS_MSG_CREATE command, enqueues
     * it on the TX queue, then busy-waits on the RX queue for the
     * matching session-create response.  On success, stores the
     * backend_sess_id for subsequent send/recv calls.
     *
     * @param ip      Destination IPv4 address (network byte order).
     * @param port    Destination port (host byte order).
     * @param devId   Backend device selector written into
     *                SessIPv4Params.device_selection.  0xFF (default) is
     *                the "auto handover" sentinel which lets the backend
     *                pick a NIC; pass an explicit id to force a specific
     *                interface in multi-NIC deployments.
     * @return true on success, false on failure.
     */
    bool connect(const IP4Addr& ip,
                 adl::uint16_t  port,
                 adl::uint16_t  devId = 0xFF);

    /**
     * Send `len` bytes through the established session.
     *
     * Data is fragmented into HyperAMP queue slots as needed (each slot
     * carries up to kHyperAmpMaxPayload bytes of user data after the 8-byte
     * header).  Enqueue is retried on HYPERAMP_AGAIN (queue full).
     *
     * @return Number of bytes sent, or -1 on error.
     */
    adl::int64_t send(const void* buf, adl::size_t len);

    /**
     * Receive exactly `len` bytes from the established session.
     *
     * Busy-waits (polls) the RX queue until enough HYPERAMP_MSG_TYPE_DATA
     * messages have arrived to fill the caller's buffer.  Partial trailing
     * data from oversized messages is kept in an internal spill buffer for
     * the next call.
     *
     * @return Number of bytes received (always == len on success), or -1.
     */
    adl::int64_t recv(void* buf, adl::size_t len);

    /**
     * Close the active session.
     *
     * Sends a HYPERAMP_MSG_TYPE_SESS / SESS_MSG_CLOSE command and waits for
     * the response.
     */
    void close();

    /**
     * @return true if a session is currently active.
     */
    bool hasSession() const { return sessionActive_; }

private:
    /* ---- Construction (private – singleton) ------------------------------ */
    HyperAmpBridge();
    ~HyperAmpBridge() = default;

    /* ---- Internal helpers ------------------------------------------------ */

    /**
     * Enqueue raw bytes to the TX queue.  Retries on HYPERAMP_AGAIN.
     * @return true on success.
     */
    bool txEnqueue(const void* data, adl::size_t len);

    /**
     * Blocking dequeue from the RX queue.
     * Polls until HYPERAMP_OK.
     *
     * @param[out] buf      Destination buffer (must be >= kHyperAmpBlockSize).
     * @param[out] outLen   Actual number of bytes dequeued.
     * @return true on success.
     */
    bool rxDequeueBlocking(void* buf, adl::size_t* outLen);

    /**
     * Allocate a fresh frontend_sess_id.  Simple monotonic counter.
     */
    adl::uint16_t nextFrontendSessId();

    /* ---- State ---------------------------------------------------------- */

    bool initialized_     = false;
    bool sessionActive_   = false;

    volatile HyperampShmQueue*  txQueue_     = nullptr;
    volatile HyperampShmQueue*  rxQueue_     = nullptr;
    volatile void*              dataRegion_  = nullptr;

    adl::uint16_t frontendSessId_ = 0;
    adl::uint16_t backendSessId_  = 0;
    adl::uint16_t sessIdCounter_  = 1;   // monotonic counter for session IDs

    /*
     * Spill buffer for recv().
     *
     * When an RX dequeue delivers more payload bytes than the caller
     * requested, the surplus is stashed here and drained on the next recv().
     */
    static constexpr adl::size_t kSpillCapacity = kHyperAmpMaxPayload;
    adl::uint8_t spillBuf_[kSpillCapacity];
    adl::size_t  spillLen_    = 0;   // valid bytes in spillBuf_
    adl::size_t  spillOffset_ = 0;   // read cursor within spillBuf_
};


}  // namespace monkey::net
