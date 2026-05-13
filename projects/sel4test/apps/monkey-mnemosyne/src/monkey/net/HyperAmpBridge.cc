/*
 * HyperAMP Network Bridge – Implementation
 *
 * See HyperAmpBridge.h for the design rationale.
 *
 * Created 2025 – Monkey-Mnemosyne / seL4 HyperAMP integration
 *
 * gongty [at] tongji [dot] edu [dot] cn
 */

#include <monkey/net/HyperAmpBridge.h>
#include <monkey/net/channel_ch2.h>
#include <monkey/log.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "hyperamp_shm_queue.h"
#include "hyperamp_protocol_defs.h"
#include <sel4/sel4.h>
}

namespace monkey::net {


/* ======================================================================== */
/*  Diagnostics                                                             */
/* ======================================================================== */
/*
 * These helpers exist purely to make field debugging tractable.  They are
 * always-on (printf-routed → seL4_DebugPutChar) because the connect/teardown
 * path runs at most a handful of times during the lab demo and the perf cost
 * is irrelevant.  Anything that runs in a tight loop (e.g. rxDequeueBlocking)
 * uses kRxPollHeartbeatPeriod throttling so it cannot flood the console.
 *
 * If you need to silence the diagnostics for production, gate them on
 * MONKEY_CFG_ENABLE_DEBUG_LOGS rather than ripping the calls out – the
 * value of being able to see "did we reach txEnqueue?" outweighs a few
 * extra log lines.
 */
namespace {

/* How often (in spin iterations) rxDequeueBlocking emits a heartbeat. */
constexpr adl::int64_t kRxPollHeartbeatPeriod = 1'000'000;

}  // namespace


/* ======================================================================== */
/*  Singleton                                                               */
/* ======================================================================== */

HyperAmpBridge::HyperAmpBridge() {
    memset(spillBuf_, 0, sizeof(spillBuf_));
}

HyperAmpBridge& HyperAmpBridge::instance() {
    static HyperAmpBridge inst;
    return inst;
}


/* ======================================================================== */
/*  Initialisation                                                          */
/* ======================================================================== */

/*
 * Internal helper – performs the queue-init dance once we have all the
 * vaddrs / paddrs / capacity figured out.  Both public init() overloads
 * funnel through here.
 */
namespace {

bool initQueuesImpl(volatile HyperampShmQueue* tx,
                    volatile HyperampShmQueue* rx,
                    uint64_t txPhys,
                    uint64_t rxPhys,
                    uint16_t capacity)
{
    HyperampQueueConfig txCfg;
    memset(&txCfg, 0, sizeof(txCfg));
    txCfg.map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH;
    txCfg.capacity   = capacity;
    txCfg.block_size = 4096;
    txCfg.phy_addr   = txPhys;
    txCfg.virt_addr  = reinterpret_cast<uint64_t>(tx);

    if (hyperamp_queue_init(tx, &txCfg, 1) != HYPERAMP_OK) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] Failed to init TX queue");
        return false;
    }

    HyperampQueueConfig rxCfg;
    memset(&rxCfg, 0, sizeof(rxCfg));
    rxCfg.map_mode   = HYPERAMP_MAP_MODE_CONTIGUOUS_BOTH;
    rxCfg.capacity   = capacity;
    rxCfg.block_size = 4096;
    rxCfg.phy_addr   = rxPhys;
    rxCfg.virt_addr  = reinterpret_cast<uint64_t>(rx);

    if (hyperamp_queue_init(rx, &rxCfg, 1) != HYPERAMP_OK) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] Failed to init RX queue");
        return false;
    }

    return true;
}

}  // namespace


void HyperAmpBridge::init(adl::uint8_t channelId) {
    if (initialized_) {
        return;
    }

    /*
     * Legacy entry point — uses the compile-time vaddrs from
     * hyperamp_shm_queue.h.  These were correct before the kernel boot
     * loader was reworked to map the 4 MiB shm window dynamically and
     * publish vaddrs through IPC MRs; they are kept here only so that
     * lab-main.cc keeps compiling.  Production callers must use the
     * four-argument overload below.
     */
    volatile HyperampShmQueue* tx = SHM_TX_QUEUE_VADDR;
    volatile HyperampShmQueue* rx = SHM_RX_QUEUE_VADDR;
    volatile void*             dr = SHM_DATA_REGION_VA;
    uint64_t txPhys = SHM_TX_QUEUE_PADDR;
    uint64_t rxPhys = SHM_RX_QUEUE_PADDR;

    if (channelId == 2) {
        /* Apply the CH2 paddr offset against the legacy base.  The
         * vaddr is still the legacy CH1 vaddr — this overload cannot
         * provide a correct CH2 vaddr because it has no access to the
         * IPC MRs; if you want a working CH2 setup, use the four-arg
         * overload from mnemosyne_engine_init() instead. */
        txPhys = SHM_TX_QUEUE_PADDR + 0x300000UL;
        rxPhys = SHM_RX_QUEUE_PADDR + 0x300000UL;
        MONKEY_LOG_WARN("[HyperAmpBridge] init(channelId=2) used the "
                        "legacy compile-time vaddrs; this only works on "
                        "kernels that still map the CH1 region at the "
                        "old 0x55E000 vaddr.  Prefer init(2, tx, rx, dt) "
                        "with vaddrs from seL4_GetMR(8/9/10).");
    }

    txQueue_    = tx;
    rxQueue_    = rx;
    dataRegion_ = dr;

    MONKEY_LOG_INFO("[HyperAmpBridge] (legacy) TX : ",
                    (unsigned long)(uintptr_t)txQueue_);
    MONKEY_LOG_INFO("[HyperAmpBridge] (legacy) RX : ",
                    (unsigned long)(uintptr_t)rxQueue_);
    MONKEY_LOG_INFO("[HyperAmpBridge] (legacy) DT : ",
                    (unsigned long)(uintptr_t)dataRegion_);

    if (!initQueuesImpl(txQueue_, rxQueue_, txPhys, rxPhys, /*capacity=*/256)) {
        return;
    }

    initialized_ = true;
    MONKEY_LOG_INFO("[HyperAmpBridge] Initialised OK (legacy path, ch=",
                    (int)channelId, ")");
}


void HyperAmpBridge::init(adl::uint8_t  channelId,
                          adl::uint64_t txVa,
                          adl::uint64_t rxVa,
                          adl::uint64_t dataVa)
{
    if (initialized_) {
        return;
    }
    if (!txVa || !rxVa || !dataVa) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] init(): null vaddr from MR");
        return;
    }

    /*
     * Pick paddr offset & queue capacity from the channel id.
     *
     * The platform shm base lives in hyperamp_shm_queue.h as
     * SHM_TX_QUEUE_PADDR / SHM_RX_QUEUE_PADDR (per CONFIG_PLAT_*); the
     * per-channel offset matches the layout enforced by the kernel boot
     * loader (CH1 = +0x200000, CH2 = +0x300000).
     */
    uint64_t paddrOffset = 0;
    uint16_t capacity    = 256;

    switch (channelId) {
        case 2:
            paddrOffset = MNEMOSYNE_CH2_PADDR_OFFSET;
            capacity    = MNEMOSYNE_CH2_QUEUE_CAPACITY;
            MONKEY_LOG_INFO("[HyperAmpBridge] Using channel 2 (mnemosyne)");
            break;
        case 1:
        default:
            paddrOffset = 0x200000UL;   /* matches HYPERAMP_CH1_OFFSET_PADDR */
            capacity    = 253;
            MONKEY_LOG_INFO("[HyperAmpBridge] Using channel 1");
            break;
    }

    txQueue_    = reinterpret_cast<volatile HyperampShmQueue*>(txVa);
    rxQueue_    = reinterpret_cast<volatile HyperampShmQueue*>(rxVa);
    dataRegion_ = reinterpret_cast<volatile void*>(dataVa);

    uint64_t txPhys = SHM_TX_QUEUE_PADDR + paddrOffset;
    uint64_t rxPhys = SHM_RX_QUEUE_PADDR + paddrOffset;

    MONKEY_LOG_INFO("[HyperAmpBridge] TX vaddr=",
                    (unsigned long)(uintptr_t)txQueue_,
                    "  paddr=", (unsigned long)txPhys);
    MONKEY_LOG_INFO("[HyperAmpBridge] RX vaddr=",
                    (unsigned long)(uintptr_t)rxQueue_,
                    "  paddr=", (unsigned long)rxPhys);
    MONKEY_LOG_INFO("[HyperAmpBridge] DT vaddr=",
                    (unsigned long)(uintptr_t)dataRegion_);

    if (!initQueuesImpl(txQueue_, rxQueue_, txPhys, rxPhys, capacity)) {
        return;
    }

    initialized_ = true;
    MONKEY_LOG_INFO("[HyperAmpBridge] Initialised OK (ch=", (int)channelId,
                    ", capacity=", (int)capacity, ")");
}


/* ======================================================================== */
/*  Internal helpers                                                        */
/* ======================================================================== */

adl::uint16_t HyperAmpBridge::nextFrontendSessId() {
    adl::uint16_t id = sessIdCounter_++;
    if (sessIdCounter_ == 0xFFFF) {   // 0xFFFF is the handover sentinel
        sessIdCounter_ = 1;
    }
    return id;
}


bool HyperAmpBridge::txEnqueue(const void* data, adl::size_t len) {
    /*
     * Retry loop: the queue may be momentarily full (HYPERAMP_AGAIN).
     * Since performance is not a concern we simply spin.
     */
    for (;;) {
        int rc = hyperamp_queue_enqueue(
            txQueue_,
            HYPERAMP_ZONE_ID_SEL4,
            data,
            len,
            dataRegion_
        );

        if (rc == HYPERAMP_OK) {
            return true;
        }
        if (rc == HYPERAMP_AGAIN) {
            /* Queue full – spin. */
            continue;
        }
        /* Hard error. */
        MONKEY_LOG_ERROR("[HyperAmpBridge] txEnqueue error, rc=", rc);
        return false;
    }
}


bool HyperAmpBridge::rxDequeueBlocking(void* buf, adl::size_t* outLen) {
    /*
     * Busy-wait until HYPERAMP_OK.
     *
     * DIAGNOSTIC: emit a throttled heartbeat (every kRxPollHeartbeatPeriod
     * empty polls) so a stuck connect() / recv() shows up on the seL4
     * console as a periodic "still waiting…" line, with the current
     * head/tail of the RX queue.  Without this the symptom is "totally
     * silent serial output" which is indistinguishable from a kernel
     * hang.
     */
    adl::int64_t pollCount = 0;
    for (;;) {
        /*
         * Invalidate the cache covering the queue control block before
         * every poll attempt so we see the latest writes from Linux.
         */
        hyperamp_cache_invalidate(rxQueue_, 64);

        size_t actual = 0;
        int rc = hyperamp_queue_dequeue(
            rxQueue_,
            HYPERAMP_ZONE_ID_SEL4,
            buf,
            kHyperAmpBlockSize,
            &actual,
            dataRegion_
        );

        if (rc == HYPERAMP_OK) {
            if (outLen) {
                *outLen = actual;
            }
            return true;
        }
        if (rc == HYPERAMP_AGAIN) {
            /* Queue empty – keep polling, with a throttled heartbeat. */
            ++pollCount;
            if (pollCount % kRxPollHeartbeatPeriod == 0) {
                
            }
            continue;
        }
        MONKEY_LOG_ERROR("[HyperAmpBridge] rxDequeueBlocking error, rc=", rc);
        return false;
    }
}


/* ======================================================================== */
/*  Session create (connect)                                                */
/* ======================================================================== */

bool HyperAmpBridge::connect(const IP4Addr& ip,
                             adl::uint16_t  port,
                             adl::uint16_t  devId) {
    if (!initialized_) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] connect() called before init()");
        return false;
    }

    if (sessionActive_) {
        MONKEY_LOG_WARN("[HyperAmpBridge] connect() – closing existing session first");
        close();
    }

    /* Reset spill buffer. */
    spillLen_    = 0;
    spillOffset_ = 0;

    frontendSessId_ = nextFrontendSessId();

    /*
     * DIAGNOSTIC: log the entry state of both queues so that, when the
     * connect() call later hangs in rxDequeueBlocking, we can correlate
     * with the Linux side and answer "did our enqueue make it across?".
     * See dumpQueueState() at the top of this file.
     */
    printf("[HyperAmpBridge][diag] connect(): ip=%s port=%u dev_id=0x%x fe_id=%u\n",
           ip.toString().c_str(), (unsigned)port, (unsigned)devId,
           (unsigned)frontendSessId_);

    /*
     * Build the session-create message.
     *
     * Wire format (big picture):
     *   [ HyperampMsgHeader (8B) ][ SessMsgHeader (10B) ][ SessIPv4Params (10B) ]
     *
     * Total = 28 bytes, well within a single 4096-byte queue slot.
     */

    /* --- SessMsgHeader --- */
    SessMsgHeader sessHdr;
    memset(&sessHdr, 0, sizeof(sessHdr));
    sessHdr.version      = PROXY_PROTO_SESS_VERSION_1;
    sessHdr.msg_type     = SESS_MSG_CREATE;
    sessHdr.action_type  = ACTION_TYPE_COMMAND;
    sessHdr.ip_version   = SESS_IPV4_PROTO;
    sessHdr.payload_len  = static_cast<uint16_t>(sizeof(SessIPv4Params));

    /* --- SessIPv4Params --- */
    SessIPv4Params sessParams;
    memset(&sessParams, 0, sizeof(sessParams));
    sessParams.device_selection      = devId;             // 0xFF = DEV_ID_AUTO_HANDOVER
    sessParams.transport_layer_proto = SESS_TCP_PROTO;
    sessParams.dest_endpoint.ipv4_addr.data[0] = ip.ui8arr[0];
    sessParams.dest_endpoint.ipv4_addr.data[1] = ip.ui8arr[1];
    sessParams.dest_endpoint.ipv4_addr.data[2] = ip.ui8arr[2];
    sessParams.dest_endpoint.ipv4_addr.data[3] = ip.ui8arr[3];
    sessParams.dest_endpoint.port    = port;

    /* --- Assemble the full message --- */
    uint8_t msgBuf[kHyperAmpBlockSize];
    memset(msgBuf, 0, sizeof(msgBuf));

    /* Outer HyperAMP header. */
    HyperampMsgHeader* outerHdr = reinterpret_cast<HyperampMsgHeader*>(msgBuf);
    outerHdr->version           = PROXY_PROTO_VERSION_1;
    outerHdr->proxy_msg_type    = static_cast<uint8_t>(HYPERAMP_MSG_TYPE_SESS);
    outerHdr->frontend_sess_id  = frontendSessId_;
    outerHdr->backend_sess_id   = 0xFFFF;   // handover sentinel
    outerHdr->payload_len       = static_cast<uint16_t>(sizeof(SessMsgHeader) + sizeof(SessIPv4Params));

    /* Session sub-header + params follow the outer header. */
    memcpy(msgBuf + HYPERAMP_MSG_HDR_SIZE,
                &sessHdr, sizeof(sessHdr));
    memcpy(msgBuf + HYPERAMP_MSG_HDR_SIZE + sizeof(SessMsgHeader),
                &sessParams, sizeof(sessParams));

    adl::size_t totalLen = HYPERAMP_MSG_HDR_SIZE + sizeof(SessMsgHeader) + sizeof(SessIPv4Params);

    MONKEY_LOG_INFO("[HyperAmpBridge] Sending SESS_CREATE  fe_id=", frontendSessId_,
                    "  ip=", ip.toString().c_str(), ":", port);

    if (!txEnqueue(msgBuf, totalLen)) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] connect() – txEnqueue failed");
        return false;
    }


    /*
     * Poll for the session-create response.
     *
     * The response is a HYPERAMP_MSG_TYPE_SESS message whose
     * frontend_sess_id matches ours.  The backend_sess_id in the
     * response header gives us the server-side session handle.
     *
     * Payload is a SessOpRespData (2 bytes: status + code).
     */
    printf("[HyperAmpBridge][diag] connect(): waiting for SESS_CREATE response (fe_id=%u)\n",
           (unsigned)frontendSessId_);
    uint8_t rxBuf[kHyperAmpBlockSize];
    for (;;) {
        adl::size_t rxLen = 0;
        if (!rxDequeueBlocking(rxBuf, &rxLen)) {
            MONKEY_LOG_ERROR("[HyperAmpBridge] connect() – rxDequeue failed");
            return false;
        }

        if (rxLen < HYPERAMP_MSG_HDR_SIZE) {
            continue;   // Malformed – skip.
        }

        const HyperampMsgHeader* rspHdr =
            reinterpret_cast<const HyperampMsgHeader*>(rxBuf);

        /* Is this our session response? */
        if (rspHdr->proxy_msg_type != static_cast<uint8_t>(HYPERAMP_MSG_TYPE_SESS)) {
            MONKEY_LOG_WARN("[HyperAmpBridge] connect() – ignoring non-SESS msg, type=",
                            (int)rspHdr->proxy_msg_type);
            continue;
        }
        if (rspHdr->frontend_sess_id != frontendSessId_) {
            MONKEY_LOG_WARN("[HyperAmpBridge] connect() – fe_id mismatch, got ",
                            rspHdr->frontend_sess_id, " want ", frontendSessId_);
            continue;
        }

        backendSessId_ = rspHdr->backend_sess_id;

        /* Check the embedded SessOpRespData if present. */
        adl::size_t payloadOff = HYPERAMP_MSG_HDR_SIZE + sizeof(SessMsgHeader);
        if (rxLen >= payloadOff + sizeof(SessOpRespData)) {
            const SessOpRespData* resp =
                reinterpret_cast<const SessOpRespData*>(rxBuf + payloadOff);
            if (resp->status != SESS_OP_STATUS_SUCCESS) {
                MONKEY_LOG_ERROR("[HyperAmpBridge] connect() – backend refused, status=",
                                 (int)resp->status, " code=", (int)resp->code);
                return false;
            }
        }

        sessionActive_ = true;
        MONKEY_LOG_INFO("[HyperAmpBridge] Session established  fe=", frontendSessId_,
                        " be=", backendSessId_);
        return true;
    }
}


/* ======================================================================== */
/*  Data send                                                               */
/* ======================================================================== */

adl::int64_t HyperAmpBridge::send(const void* buf, adl::size_t len) {
    if (!initialized_ || !sessionActive_) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] send() – no active session");
        return -1;
    }

    const uint8_t* src = static_cast<const uint8_t*>(buf);
    adl::size_t    remaining = len;
    adl::size_t    totalSent = 0;

    while (remaining > 0) {
        adl::size_t chunk = remaining;
        if (chunk > kHyperAmpMaxPayload) {
            chunk = kHyperAmpMaxPayload;
        }

        /* Build a HYPERAMP_MSG_TYPE_DATA message. */
        uint8_t msgBuf[kHyperAmpBlockSize];
        memset(msgBuf, 0, sizeof(msgBuf));

        HyperampMsgHeader* hdr = reinterpret_cast<HyperampMsgHeader*>(msgBuf);
        hdr->version          = PROXY_PROTO_VERSION_1;
        hdr->proxy_msg_type   = static_cast<uint8_t>(HYPERAMP_MSG_TYPE_DATA);
        hdr->frontend_sess_id = frontendSessId_;
        hdr->backend_sess_id  = backendSessId_;
        hdr->payload_len      = static_cast<uint16_t>(chunk);

        memcpy(msgBuf + HYPERAMP_MSG_HDR_SIZE, src, chunk);

        if (!txEnqueue(msgBuf, HYPERAMP_MSG_HDR_SIZE + chunk)) {
            MONKEY_LOG_ERROR("[HyperAmpBridge] send() – txEnqueue failed after ",
                             totalSent, " bytes");
            return (totalSent > 0) ? static_cast<adl::int64_t>(totalSent) : -1;
        }

        src       += chunk;
        remaining -= chunk;
        totalSent += chunk;
    }

    return static_cast<adl::int64_t>(totalSent);
}


/* ======================================================================== */
/*  Data recv (blocking / polling)                                          */
/* ======================================================================== */

adl::int64_t HyperAmpBridge::recv(void* buf, adl::size_t len) {
    if (!initialized_ || !sessionActive_) {
        MONKEY_LOG_ERROR("[HyperAmpBridge] recv() – no active session");
        return -1;
    }

    uint8_t* dst       = static_cast<uint8_t*>(buf);
    adl::size_t needed = len;
    adl::size_t got    = 0;

    /* 1. Drain any leftover bytes from the spill buffer first. */
    if (spillLen_ > spillOffset_) {
        adl::size_t avail = spillLen_ - spillOffset_;
        adl::size_t take  = (avail <= needed) ? avail : needed;
        memcpy(dst, spillBuf_ + spillOffset_, take);
        spillOffset_ += take;
        dst    += take;
        needed -= take;
        got    += take;

        if (spillOffset_ >= spillLen_) {
            spillLen_    = 0;
            spillOffset_ = 0;
        }
    }

    /* 2. Keep pulling messages until we have enough. */
    uint8_t rxBuf[kHyperAmpBlockSize];

    while (needed > 0) {
        adl::size_t rxLen = 0;
        if (!rxDequeueBlocking(rxBuf, &rxLen)) {
            MONKEY_LOG_ERROR("[HyperAmpBridge] recv() – rxDequeue failed");
            return (got > 0) ? static_cast<adl::int64_t>(got) : -1;
        }

        if (rxLen < HYPERAMP_MSG_HDR_SIZE) {
            continue;
        }

        const HyperampMsgHeader* hdr =
            reinterpret_cast<const HyperampMsgHeader*>(rxBuf);

        /* Only accept DATA messages for our session. */
        if (hdr->proxy_msg_type != static_cast<uint8_t>(HYPERAMP_MSG_TYPE_DATA)) {
            MONKEY_LOG_WARN("[HyperAmpBridge] recv() – ignoring non-DATA msg, type=",
                            (int)hdr->proxy_msg_type);
            continue;
        }

        /*
         * Accept if either session ID matches – the backend may use
         * either the frontend or backend session ID.
         */
        if (hdr->frontend_sess_id != frontendSessId_ &&
            hdr->backend_sess_id  != backendSessId_) {
            MONKEY_LOG_WARN("[HyperAmpBridge] recv() – session id mismatch");
            continue;
        }

        adl::size_t payloadLen = hdr->payload_len;
        const uint8_t* payload = rxBuf + HYPERAMP_MSG_HDR_SIZE;

        if (payloadLen == 0) {
            continue;
        }

        /* Copy as much as the caller still needs. */
        adl::size_t take = (payloadLen <= needed) ? payloadLen : needed;
        memcpy(dst, payload, take);
        dst    += take;
        needed -= take;
        got    += take;

        /* Stash surplus in the spill buffer. */
        if (take < payloadLen) {
            adl::size_t surplus = payloadLen - take;
            memcpy(spillBuf_, payload + take, surplus);
            spillLen_    = surplus;
            spillOffset_ = 0;
        }
    }

    return static_cast<adl::int64_t>(got);
}


/* ======================================================================== */
/*  Session close                                                           */
/* ======================================================================== */

void HyperAmpBridge::close() {
    if (!initialized_ || !sessionActive_) {
        return;
    }

    MONKEY_LOG_INFO("[HyperAmpBridge] Closing session fe=", frontendSessId_,
                    " be=", backendSessId_);

    /*
     * Build a SESS_MSG_CLOSE command.
     *
     * Wire format:
     *   [ HyperampMsgHeader (8B) ][ SessMsgHeader (10B) ]
     *
     * The CLOSE command has no payload beyond the session sub-header.
     */
    SessMsgHeader sessHdr;
    memset(&sessHdr, 0, sizeof(sessHdr));
    sessHdr.version     = PROXY_PROTO_SESS_VERSION_1;
    sessHdr.msg_type    = SESS_MSG_CLOSE;
    sessHdr.action_type = ACTION_TYPE_COMMAND;
    sessHdr.ip_version  = SESS_IPV4_PROTO;
    sessHdr.payload_len = 0;

    uint8_t msgBuf[kHyperAmpBlockSize];
    memset(msgBuf, 0, sizeof(msgBuf));

    HyperampMsgHeader* outerHdr = reinterpret_cast<HyperampMsgHeader*>(msgBuf);
    outerHdr->version           = PROXY_PROTO_VERSION_1;
    outerHdr->proxy_msg_type    = static_cast<uint8_t>(HYPERAMP_MSG_TYPE_SESS);
    outerHdr->frontend_sess_id  = frontendSessId_;
    outerHdr->backend_sess_id   = backendSessId_;
    outerHdr->payload_len       = static_cast<uint16_t>(sizeof(SessMsgHeader));

    memcpy(msgBuf + HYPERAMP_MSG_HDR_SIZE, &sessHdr, sizeof(sessHdr));

    adl::size_t totalLen = HYPERAMP_MSG_HDR_SIZE + sizeof(SessMsgHeader);
    txEnqueue(msgBuf, totalLen);

    /*
     * Wait for the close response.  We give it a bounded attempt count so
     * that a non-responsive backend doesn't hang the system forever.
     */
    uint8_t rxBuf[kHyperAmpBlockSize];
    static constexpr int kMaxCloseAttempts = 50000;

    for (int attempt = 0; attempt < kMaxCloseAttempts; ++attempt) {
        hyperamp_cache_invalidate(rxQueue_, 64);

        size_t actual = 0;
        int rc = hyperamp_queue_dequeue(
            rxQueue_, HYPERAMP_ZONE_ID_SEL4,
            rxBuf, kHyperAmpBlockSize, &actual, dataRegion_
        );

        if (rc == HYPERAMP_AGAIN) {
            continue;
        }
        if (rc != HYPERAMP_OK) {
            break;
        }
        if (actual < HYPERAMP_MSG_HDR_SIZE) {
            continue;
        }

        const HyperampMsgHeader* rsp =
            reinterpret_cast<const HyperampMsgHeader*>(rxBuf);

        if (rsp->proxy_msg_type == static_cast<uint8_t>(HYPERAMP_MSG_TYPE_SESS) &&
            rsp->frontend_sess_id == frontendSessId_) {
            /* Got the close ack. */
            break;
        }
    }

    sessionActive_ = false;
    spillLen_       = 0;
    spillOffset_    = 0;

    MONKEY_LOG_INFO("[HyperAmpBridge] Session closed");
}


}  // namespace monkey::net
