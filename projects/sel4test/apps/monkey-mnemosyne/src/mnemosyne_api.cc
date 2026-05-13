/**
 * @file mnemosyne_api.cc
 * @brief Pure-C facade for monkey-mnemosyne — implementation.
 *
 * Bridges plain-C callers (e.g. apps/front-style validation apps) into
 * the existing C++ Protocol2Connection client.  See
 * include/mnemosyne_api.h for the contract.
 *
 * Internally this is a thin shim:
 *
 *   mnemosyne_engine_init()
 *       -> read CH2 vaddrs from IPC MR(8/9/10)         (FIRST!)
 *       -> initAdlAllocOnce()
 *       -> HyperAmpBridge::instance().init(2, tx, rx, dt)
 *       -> remember server ip / port / auth_key / role
 *
 *   mnemosyne_sess_new()
 *       -> new Protocol2Connection
 *
 *   mnemosyne_sess_connect_by_addrstr_devid(sess, proto, "ip:port", dev_id)
 *       -> parse "ip:port"
 *       -> Socket4::connect()           (routed via HyperAmpBridge)
 *       -> Protocol1Connection::hello(2, CLIENT)
 *       -> Protocol1Connection::auth(auth_key)
 *       -> role == ALLOC : tryAlloc()    -> remember blockId
 *       -> role == REF   : refBlock(key) -> remember blockId
 *
 *   mnemosyne_sess_send() / recv()
 *       -> single 4 KiB writeBlock() / readBlock()
 *
 *   mnemosyne_sess_close()
 *       -> unrefBlock() (best-effort) + Protocol2Connection::close() + delete
 *
 * gongty [at] tongji [dot] edu [dot] cn
 */

#include "../include/mnemosyne_api.h"

#include <adl/sys/types.h>

#include <monkey/Status.h>
#include <monkey/log.h>
#include <monkey/net/HyperAmpBridge.h>
#include <monkey/net/channel_ch2.h>
#include <monkey/net/protocol.h>

#include <stdlib.h>
#include <string.h>

extern "C" {
#include <sel4/sel4.h>
}


/* ====================================================================== */
/*  Internal state                                                        */
/* ====================================================================== */

namespace {

/*
 * Global engine state.  monkey-mnemosyne does not have a per-engine
 * object; HyperAmpBridge is already a singleton, and we only need a
 * handful of scalars to remember what mnemosyne_engine_init() was called
 * with.  Keeping them file-static avoids exposing any C++ types through
 * the C ABI.
 */
struct EngineState {
    bool          inited       = false;
    char          ip[64]       = {0};
    adl::uint16_t port         = 0;
    char          authKey[128] = {0};
    adl::uint8_t  channelId    = MNEMOSYNE_CHANNEL_DEFAULT;
    adl::uint8_t  role         = MNEMOSYNE_ROLE_ALLOC;
};

EngineState g_engine;

/*
 * Predictable access key shared between the alloc-side and the ref-side.
 *
 * TODO(gty 2026): this assumes (a) exactly one alloc-side and one
 * ref-side per deployment and (b) the alloc-side runs first.  If
 * multiple sessions ever coexist or the order is not guaranteed this
 * scheme breaks.  Lift the access key out into mnemosyne_engine_init()
 * as a proper parameter once the integrator confirms the deployment
 * topology.
 */
constexpr adl::int64_t kPredictableKeyBase = 10000001;
constexpr adl::int64_t kPageWriteKeyBit    = 0x8000000000000000LL;

inline adl::int64_t predictableWriteKey() {
    return kPageWriteKeyBit + kPredictableKeyBase;
}

bool g_adlAllocInited = false;

void initAdlAllocOnce() {
    if (g_adlAllocInited) return;

    static struct {
    } adlAllocData;

    adl::defaultAllocator.init({
        .alloc = [] (adl::size_t size, void* /*data*/) -> void* {
            return malloc(size);
        },
        .free  = [] (void* addr, adl::size_t /*size*/, void* /*data*/) {
            free(addr);
        },
        .data  = &adlAllocData
    });

    g_adlAllocInited = true;
}

/*
 * Parse "ip:port" into separate ip string + numeric port.
 *
 * Mirrors what front does in IPV4_PORT_STR_TO_TUPLE: split on ':',
 * take everything before as IP, everything after (decimal) as port.
 *
 * On success returns 0 and writes the IP into ipOut[0..ipOutMax-1]
 * (NUL-terminated) and the port into *portOut.  Returns -1 on any
 * parse error.
 */
int parseIpPort(const char* s, char* ipOut, size_t ipOutMax,
                adl::uint16_t* portOut)
{
    if (!s || !ipOut || !portOut || ipOutMax < 2) return -1;

    const char* colon = strchr(s, ':');
    if (!colon || colon == s) return -1;

    size_t ipLen = static_cast<size_t>(colon - s);
    if (ipLen >= ipOutMax) return -1;

    memcpy(ipOut, s, ipLen);
    ipOut[ipLen] = '\0';

    /* Decimal port. */
    long p = 0;
    for (const char* q = colon + 1; *q; ++q) {
        if (*q < '0' || *q > '9') return -1;
        p = p * 10 + (*q - '0');
        if (p > 0xFFFF) return -1;
    }
    if (p == 0) return -1;

    *portOut = static_cast<adl::uint16_t>(p);
    return 0;
}

}  // namespace


/*
 * Concrete session struct.  Hidden behind an opaque typedef in the C
 * header so the layout never leaks.
 */
struct mnemosyne_session_s {
    monkey::net::Protocol2Connection* client = nullptr;

    adl::int64_t blockId   = -1;
    bool         hasBlock  = false;
    bool         connected = false;

    /*
     * 4 KiB staging page for send/recv.  Aligned to 4 KiB to match what
     * Protocol2Connection::readBlock / writeBlock expect (they assume
     * the caller-supplied buffer can hold a full 4 KiB page).
     */
    adl::uint8_t staging[MNEMOSYNE_BLOCK_SIZE]
        __attribute__((aligned(4096))) = {0};
};


/* ====================================================================== */
/*  Engine                                                                */
/* ====================================================================== */

extern "C" int mnemosyne_engine_init(const char *server_ip,
                                     uint16_t    server_port,
                                     const char *auth_key,
                                     uint8_t     channel_id,
                                     uint8_t     role)
{
    if (g_engine.inited) {
        return 0;   /* idempotent */
    }

    if (!server_ip || !auth_key) {
        return -1;
    }
    if (role != MNEMOSYNE_ROLE_ALLOC && role != MNEMOSYNE_ROLE_REF) {
        return -2;
    }

    /*
     * STEP 1 — read the channel virtual addresses from the IPC message
     * registers BEFORE doing anything that could trigger a syscall.
     *
     * The kernel boot loader publishes:
     *   msg[2..4]  -> CH0 (TX, RX, Data)
     *   msg[5..7]  -> CH1 (TX, RX, Data)
     *   msg[8..10] -> CH2 (TX, RX, Data)
     * (See HYPERAMP_MULTI_CHANNEL_DESIGN.md and kernel/src/arch/arm/
     *  kernel/boot.c.)  The IPC buffer is volatile — every syscall
     *  overwrites it — so we capture the values up-front and only
     *  perform syscalls afterwards.
     */
    const adl::uint8_t resolvedChannel =
        (channel_id == 0) ? MNEMOSYNE_CHANNEL_DEFAULT : channel_id;

    seL4_Word txVa = 0, rxVa = 0, dtVa = 0;
    switch (resolvedChannel) {
        case 2:
            txVa = seL4_GetMR(MNEMOSYNE_MR_SLOT_CH2_TX);
            rxVa = seL4_GetMR(MNEMOSYNE_MR_SLOT_CH2_RX);
            dtVa = seL4_GetMR(MNEMOSYNE_MR_SLOT_CH2_DATA);
            break;
        case 1:
            /* Debug / experimental: bind to CH1 instead.  The integrator
             * is responsible for ensuring nothing else (front,
             * hyperamp-server) is using CH1 in the same image. */
            txVa = seL4_GetMR(5);
            rxVa = seL4_GetMR(6);
            dtVa = seL4_GetMR(7);
            break;
        default:
            return -3;
    }

    /*
     * STEP 2 — record engine config and bring up the ADL allocator.
     */
    strncpy(g_engine.ip,      server_ip, sizeof(g_engine.ip)      - 1);
    strncpy(g_engine.authKey, auth_key,  sizeof(g_engine.authKey) - 1);
    g_engine.port      = server_port;
    g_engine.channelId = resolvedChannel;
    g_engine.role      = role;

    /* Bring up the ADL allocator after capturing IPC MR values. */
    initAdlAllocOnce();

    /*
     * STEP 3 — initialise the HyperAMP bridge with the captured vaddrs.
     */
    if (!txVa || !rxVa || !dtVa) {
        MONKEY_LOG_ERROR("[mnemosyne_api] IPC MR returned null vaddr "
                         "(boot loader did not publish channel ",
                         (int)resolvedChannel, "?)");
        return -4;
    }

    monkey::net::HyperAmpBridge::instance().init(
        resolvedChannel,
        static_cast<adl::uint64_t>(txVa),
        static_cast<adl::uint64_t>(rxVa),
        static_cast<adl::uint64_t>(dtVa));

    if (!monkey::net::HyperAmpBridge::instance().isInitialized()) {
        MONKEY_LOG_ERROR("[mnemosyne_api] HyperAmpBridge init failed");
        return -5;
    }

    g_engine.inited = true;
    MONKEY_LOG_INFO("[mnemosyne_api] engine initialised, server=",
                    g_engine.ip, ":", (unsigned int)g_engine.port,
                    " channel=", (int)g_engine.channelId,
                    " role=",    (int)g_engine.role);
    return 0;
}


/* 方案 A：接受外部传入 vaddr 的初始化入口，不再调用 seL4_GetMR() */
extern "C" int mnemosyne_engine_init_with_vaddrs(const char *server_ip,
                                                  uint16_t    server_port,
                                                  const char *auth_key,
                                                  uint8_t     channel_id,
                                                  uint8_t     role,
                                                  uint64_t    tx_va,
                                                  uint64_t    rx_va,
                                                  uint64_t    data_va)
{
    if (g_engine.inited) {
        return 0;   /* 幂等 */
    }

    if (!server_ip || !auth_key) {
        return -1;
    }
    if (role != MNEMOSYNE_ROLE_ALLOC && role != MNEMOSYNE_ROLE_REF) {
        return -2;
    }

    const adl::uint8_t resolvedChannel =
        (channel_id == 0) ? MNEMOSYNE_CHANNEL_DEFAULT : channel_id;

    /* 使用外部传入的虚拟地址，不再调用 seL4_GetMR */
    seL4_Word txVa = (seL4_Word)tx_va;
    seL4_Word rxVa = (seL4_Word)rx_va;
    seL4_Word dtVa = (seL4_Word)data_va;

    /* 记录引擎配置 */
    strncpy(g_engine.ip,      server_ip, sizeof(g_engine.ip)      - 1);
    strncpy(g_engine.authKey, auth_key,  sizeof(g_engine.authKey) - 1);
    g_engine.port      = server_port;
    g_engine.channelId = resolvedChannel;
    g_engine.role      = role;

    /* 初始化 ADL 分配器 */
    initAdlAllocOnce();

    /* 初始化 HyperAMP bridge */
    if (!txVa || !rxVa || !dtVa) {
        MONKEY_LOG_ERROR("[mnemosyne_api] vaddr is null for channel ",
                         (int)resolvedChannel);
        return -4;
    }

    monkey::net::HyperAmpBridge::instance().init(
        resolvedChannel,
        static_cast<adl::uint64_t>(txVa),
        static_cast<adl::uint64_t>(rxVa),
        static_cast<adl::uint64_t>(dtVa));

    if (!monkey::net::HyperAmpBridge::instance().isInitialized()) {
        MONKEY_LOG_ERROR("[mnemosyne_api] HyperAmpBridge init failed");
        return -5;
    }

    g_engine.inited = true;
    MONKEY_LOG_INFO("[mnemosyne_api] engine initialised (with_vaddrs), server=",
                    g_engine.ip, ":", (unsigned int)g_engine.port,
                    " channel=", (int)g_engine.channelId,
                    " role=",    (int)g_engine.role);
    return 0;
}


extern "C" void mnemosyne_engine_run_hyperamp_once(void)
{
    /*
     * monkey-mnemosyne does not maintain an event loop of its own —
     * every send / recv call drives the HyperAMP queue itself, and the
     * upper-layer application is expected to call mnemosyne_sess_recv()
     * in its own loop to detect updates (see lab-main.cc::waitDataUpdate
     * for the equivalent pattern).  This function is therefore an
     * intentional no-op, provided only so that callers can use a unified
     * loop body alongside frontend_engine_run_hyperamp_once().
     */
}


/* ====================================================================== */
/*  Session                                                               */
/* ====================================================================== */

extern "C" mnemosyne_session_t *mnemosyne_sess_new(void)
{
    if (!g_engine.inited) {
        MONKEY_LOG_ERROR("[mnemosyne_api] sess_new before engine_init");
        return nullptr;
    }

    auto* sess = adl::defaultAllocator.alloc<mnemosyne_session_s>();
    if (!sess) return nullptr;

    sess->client = adl::defaultAllocator.alloc<monkey::net::Protocol2Connection>();
    if (!sess->client) {
        adl::defaultAllocator.free(sess);
        return nullptr;
    }

    /* Pre-fill with engine-default endpoint; overridden in connect(). */
    sess->client->ip.set(g_engine.ip);
    sess->client->port = g_engine.port;

    return sess;
}


extern "C" int mnemosyne_sess_connect_by_addrstr_devid(
    mnemosyne_session_t *sess,
    int          proto,
    const char  *addr_str,
    uint16_t     dev_id)
{
    if (!sess || !sess->client) {
        return -1;
    }

    /*
     * monkey-mnemosyne speaks TCP only.  We accept the proto parameter
     * for API symmetry with frontend_sess_connect_by_addrstr_devid but
     * refuse anything other than TCP.
     */
    if (proto != MNEMOSYNE_PROTO_TCP) {
        MONKEY_LOG_ERROR("[mnemosyne_api] only TCP supported, got proto=",
                         proto);
        return -2;
    }

    auto& client = *sess->client;

    /*
     * Parse "ip:port".  An empty / NULL addr_str means "reuse engine
     * defaults" (mirrors the convenience knob lab-main.cc had).
     */
    if (addr_str && addr_str[0] != '\0') {
        char ip[64];
        adl::uint16_t port = 0;
        if (parseIpPort(addr_str, ip, sizeof(ip), &port) != 0) {
            MONKEY_LOG_ERROR("[mnemosyne_api] bad addr_str (need 'ip:port'): ",
                             addr_str);
            return -3;
        }
        client.ip.set(ip);
        client.port = port;
    }

    using HelloMode = monkey::net::ProtocolConnection::HelloMode;

    /*
     * connect() goes through Socket4::connect() -> HyperAmpBridge.connect()
     * which builds the SESS_CREATE message.  We need to forward dev_id
     * into SessIPv4Params.device_selection — the existing Socket4 path
     * uses the bridge's default 0xFF, so for non-default dev_id we have
     * to call the bridge directly.
     */
    monkey::Status st;

    if (dev_id == MNEMOSYNE_DEV_AUTO) {
        st = client.connect();    /* legacy path: bridge.connect uses 0xFF */
    } else {
        /*
         * Bypass Socket4::connect()'s hard-coded auto dev_id by talking
         * to the bridge ourselves with the explicit dev_id, then mark
         * the socket "valid" the same way Socket4::connect() does.
         */
        client.close();
        auto& bridge = monkey::net::HyperAmpBridge::instance();
        /* Bridge must already be up (mnemosyne_engine_init brought it
         * up via the 4-arg init overload); no need to call init() again. */
        bool ok = bridge.connect(client.ip, client.port, dev_id);
        if (!ok) {
            MONKEY_LOG_ERROR("[mnemosyne_api] bridge.connect (dev_id=",
                             (int)dev_id, ") failed");
            return -4;
        }
        client.socketFd = 9999;  /* same sentinel Socket4::connect uses */
        st = monkey::Status::SUCCESS;
    }

    if (st != monkey::Status::SUCCESS) {
        MONKEY_LOG_ERROR("[mnemosyne_api] connect failed");
        return -4;
    }

    st = client.hello(monkey::net::Protocol2Connection::VERSION,
                      HelloMode::CLIENT);
    if (st != monkey::Status::SUCCESS) {
        MONKEY_LOG_ERROR("[mnemosyne_api] hello failed");
        client.close();
        return -5;
    }

    st = client.auth(g_engine.authKey);
    if (st != monkey::Status::SUCCESS) {
        MONKEY_LOG_ERROR("[mnemosyne_api] auth failed");
        client.close();
        return -6;
    }

    sess->connected = true;

    /* Acquire the shared 4 KiB block according to role. */
    if (g_engine.role == MNEMOSYNE_ROLE_ALLOC) {
        adl::int64_t bid = -1;
        st = client.tryAlloc(&bid);
        if (st != monkey::Status::SUCCESS) {
            MONKEY_LOG_ERROR("[mnemosyne_api] tryAlloc failed");
            client.close();
            sess->connected = false;
            return -7;
        }
        sess->blockId  = bid;
        sess->hasBlock = true;
        MONKEY_LOG_INFO("[mnemosyne_api] alloc-side blockId=",
                        (long)sess->blockId);
    } else {
        /*
         * Ref-side: try once.  Mirrors the loop in lab-main.cc::doLabApp2,
         * but only one attempt — the upper-layer application is expected
         * to retry mnemosyne_sess_connect_by_addrstr_devid() if refBlock
         * isn't ready yet (alloc-side hasn't run).  This keeps the API
         * non-blocking-ish.
         */
        adl::int64_t bid = -1;
        st = client.refBlock(predictableWriteKey(), &bid);
        if (st != monkey::Status::SUCCESS) {
            MONKEY_LOG_WARN("[mnemosyne_api] refBlock not ready yet "
                            "(alloc-side may not have run)");
            client.close();
            sess->connected = false;
            return -8;
        }
        sess->blockId  = bid;
        sess->hasBlock = true;
        MONKEY_LOG_INFO("[mnemosyne_api] ref-side blockId=",
                        (long)sess->blockId);
    }

    return 0;
}


extern "C" int mnemosyne_sess_send(mnemosyne_session_t *sess,
                                   const void *data, uint32_t size)
{
    if (!sess || !sess->client || !sess->connected || !sess->hasBlock) {
        return -1;
    }
    if (!data || size == 0) {
        return -2;
    }
    if (size > MNEMOSYNE_BLOCK_SIZE) {
        MONKEY_LOG_ERROR("[mnemosyne_api] send size > 4096 not supported");
        return -3;
    }

    /* Stage the payload into our 4 KiB page (zero-pad the tail). */
    memcpy(sess->staging, data, size);
    if (size < MNEMOSYNE_BLOCK_SIZE) {
        memset(sess->staging + size, 0, MNEMOSYNE_BLOCK_SIZE - size);
    }

    monkey::Status st = sess->client->writeBlock(sess->blockId, sess->staging);
    if (st != monkey::Status::SUCCESS) {
        MONKEY_LOG_ERROR("[mnemosyne_api] writeBlock failed");
        return -4;
    }
    return (int)size;
}


extern "C" int mnemosyne_sess_recv(mnemosyne_session_t *sess,
                                   void *buf, uint32_t size)
{
    if (!sess || !sess->client || !sess->connected || !sess->hasBlock) {
        return -1;
    }
    if (!buf || size == 0) {
        return -2;
    }

    monkey::Status st = sess->client->readBlock(sess->blockId, sess->staging);
    if (st != monkey::Status::SUCCESS) {
        MONKEY_LOG_ERROR("[mnemosyne_api] readBlock failed");
        return -3;
    }

    uint32_t copy = (size < MNEMOSYNE_BLOCK_SIZE) ? size : MNEMOSYNE_BLOCK_SIZE;
    memcpy(buf, sess->staging, copy);
    return (int)copy;
}


extern "C" int mnemosyne_sess_send_plain_text(mnemosyne_session_t *sess, const char *text, int64_t* checksum)
{
    if (!sess->connected) {
        Genode::error("[ERROR] Session not connected to Monkey Mnemosyne!");
        return 1;
    }

    adl::TString str {text};
    int status = (int) sess->client->plainText(str, checksum);

    return status;
}


extern "C" void mnemosyne_sess_close(mnemosyne_session_t *sess)
{
    if (!sess) return;

    if (sess->client) {
        if (sess->hasBlock) {
            /* Best-effort unref; we ignore errors because we're tearing
             * the session down regardless. */
            sess->client->unrefBlock(sess->blockId);
        }
        if (sess->connected) {
            sess->client->close();
        }
        adl::defaultAllocator.free(sess->client);
        sess->client = nullptr;
    }

    adl::defaultAllocator.free(sess);
}
