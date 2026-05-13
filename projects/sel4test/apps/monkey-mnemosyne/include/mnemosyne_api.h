/**
 * @file mnemosyne_api.h
 * @brief Pure-C facade for monkey-mnemosyne, mirroring the apps/front
 *        (network proxy) API surface for unified upper-layer integration.
 *
 * Design intent
 * -------------
 *   The integration spec asks monkey-mnemosyne to expose a small set of
 *   functions whose *shape* matches the proxy frontend's
 *   frontend_engine_init / frontend_sess_new /
 *   frontend_sess_connect_by_addrstr_devid / frontend_sess_send /
 *   frontend_sess_recv / frontend_engine_run_hyperamp_once /
 *   frontend_sess_close.  The semantics underneath remain the existing
 *   monkey-mnemosyne client (Protocol2Connection over HyperAMP), with a
 *   single shared 4 KiB block as the data carrier.
 *
 *   Compared with apps/front the only signature differences are:
 *     - functions are prefixed `mnemosyne_` so they can coexist with
 *       `frontend_*` in the same translation unit;
 *     - there is no `engine` handle; the engine state is global because
 *       the underlying HyperAmpBridge is itself a singleton.
 *
 * Linkage
 * -------
 *   This header is C-clean (extern "C", no C++ types, opaque session
 *   handle).  Upper-layer C code only needs:
 *
 *       #include <mnemosyne_api.h>
 *       ... + link against monkey-mnemosyne-lib
 *
 *   The implementation is C++ but the ABI exported is plain C.
 *
 * Concurrency
 * -----------
 *   The underlying HyperAmpBridge is a singleton; only ONE *connected*
 *   session may exist at a time.  Calling mnemosyne_sess_new() twice
 *   without closing the first session is undefined behaviour.
 *
 * Roles (alloc vs ref)
 * --------------------
 *   monkey-mnemosyne shares a single 4 KiB page between two endpoints:
 *     - the "alloc" endpoint  : calls tryAlloc to create the page;
 *     - the "ref"   endpoint  : calls refBlock to attach to it.
 *   The role is fixed at engine init time via `role` in
 *   mnemosyne_engine_init().  Block id and access key are currently
 *   predictable (see TODO in mnemosyne_api.cc).
 *
 * IPC-MR ordering — IMPORTANT
 * ---------------------------
 *   mnemosyne_engine_init() reads the CH2 virtual addresses from the IPC
 *   message registers (seL4_GetMR(8/9/10), set up by the kernel boot
 *   loader).  The IPC buffer is volatile across syscalls, so the call
 *   MUST happen BEFORE the host program issues any seL4 syscall.  In
 *   practice that means it should be the very first thing your `main()`
 *   does, exactly as apps/hyperamp-server/src/main.c reads MR(2..7) at
 *   the top.
 *
 * gongty [at] tongji [dot] edu [dot] cn
 */

#ifndef MNEMOSYNE_API_H
#define MNEMOSYNE_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================== */
/*  Constants                                                             */
/* ====================================================================== */

/** Role: this endpoint allocates the shared block (issues tryAlloc). */
#define MNEMOSYNE_ROLE_ALLOC   1

/** Role: this endpoint references an already-allocated block. */
#define MNEMOSYNE_ROLE_REF     2

/**
 * HyperAMP channel id.  CH2 is reserved for monkey-mnemosyne by
 * HYPERAMP_MULTI_CHANNEL_DESIGN.md.  Pass 0 to mnemosyne_engine_init()
 * to mean "default" which resolves to CH2.
 */
#define MNEMOSYNE_CHANNEL_DEFAULT  2

/**
 * Sentinel for "no specific NIC selection on backend".  Same numeric
 * value as DEV_ID_AUTO_HANDOVER used throughout apps/front.
 */
#define MNEMOSYNE_DEV_AUTO     0xFF

/** The data carrier is a single 4 KiB page; send/recv length must fit. */
#define MNEMOSYNE_BLOCK_SIZE   4096

/**
 * Transport protocol values, kept binary-compatible with apps/front's
 * SessTranProto enum (SESS_UDP_PROTO=0, SESS_TCP_PROTO=1).  monkey-
 * mnemosyne itself only uses TCP at the moment but the parameter is
 * accepted for API symmetry with frontend_sess_connect_by_addrstr_devid.
 */
#define MNEMOSYNE_PROTO_UDP    0
#define MNEMOSYNE_PROTO_TCP    1

/* ====================================================================== */
/*  Opaque types                                                          */
/* ====================================================================== */

typedef struct mnemosyne_session_s mnemosyne_session_t;

/* ====================================================================== */
/*  Engine                                                                */
/* ====================================================================== */

/**
 * Initialise the mnemosyne engine.  Counterpart of frontend_engine_init().
 *
 *   - Initialises the ADL allocator (idempotent).
 *   - Reads the CH2 shared-memory virtual addresses from the IPC message
 *     registers (seL4_GetMR(8/9/10)).  See "IPC-MR ordering" in the file
 *     header — this must happen before any other syscall.
 *   - Initialises the HyperAMP bridge on the requested channel.
 *   - Stores the server endpoint, auth key and role for use by sessions
 *     created via mnemosyne_sess_new().
 *
 * Idempotent: subsequent calls return 0 without reinitialising.
 *
 * @param server_ip   IPv4 dotted string (e.g. "10.0.0.5").  Copied
 *                    internally; may be freed by the caller afterwards.
 * @param server_port Server TCP port (host byte order).
 * @param auth_key    UUID-style string (e.g. lab-main App1Key).  Copied
 *                    internally.
 * @param channel_id  HyperAMP channel: 0 = default (= MNEMOSYNE_CHANNEL_
 *                    DEFAULT = CH2), 2 = CH2 explicitly, 1 = CH1 (debug
 *                    / experimental only).
 * @param role        MNEMOSYNE_ROLE_ALLOC or MNEMOSYNE_ROLE_REF.
 *
 * @return 0 on success; <0 on error.
 */
int mnemosyne_engine_init(const char *server_ip,
                          uint16_t    server_port,
                          const char *auth_key,
                          uint8_t     channel_id,
                          uint8_t     role);

/**
 * 方案 A 变体：接受外部传入的共享内存虚拟地址。
 *
 * 与 mnemosyne_engine_init() 完全相同，但不再内部调用 seL4_GetMR()，
 * 而是直接使用调用者提供的 tx_va / rx_va / data_va。
 *
 * 这是 hyperamp-server 集成时的推荐入口：main.c 在最开头统一读取
 * 所有 IPC MR(2..10)，然后把 CH2 的三个地址传进来。
 *
 * @param server_ip   IPv4 dotted string.
 * @param server_port Server TCP port (host byte order).
 * @param auth_key    UUID-style auth string.
 * @param channel_id  HyperAMP channel (通常传 2).
 * @param role        MNEMOSYNE_ROLE_ALLOC or MNEMOSYNE_ROLE_REF.
 * @param tx_va       CH2 TX queue 虚拟地址 (seL4_GetMR(8)).
 * @param rx_va       CH2 RX queue 虚拟地址 (seL4_GetMR(9)).
 * @param data_va     CH2 Data region 虚拟地址 (seL4_GetMR(10)).
 *
 * @return 0 on success; <0 on error.
 */
int mnemosyne_engine_init_with_vaddrs(const char *server_ip,
                                      uint16_t    server_port,
                                      const char *auth_key,
                                      uint8_t     channel_id,
                                      uint8_t     role,
                                      uint64_t    tx_va,
                                      uint64_t    rx_va,
                                      uint64_t    data_va);

/**
 * Single-shot scheduler hook, mirrors frontend_engine_run_hyperamp_once().
 *
 * monkey-mnemosyne does not need an engine event loop: every send/recv
 * call drives the HyperAMP queue itself.  This function is provided for
 * API symmetry only; it is intentionally a no-op so that integrators can
 * write a unified main loop:
 *
 *     while (running) {
 *         frontend_engine_run_hyperamp_once();
 *         mnemosyne_engine_run_hyperamp_once();
 *         ... pull / push session data ...
 *     }
 */
void mnemosyne_engine_run_hyperamp_once(void);

/* ====================================================================== */
/*  Session                                                               */
/* ====================================================================== */

/**
 * Allocate a new session object.  Counterpart of frontend_sess_new().
 *
 * The returned handle is owned by the caller and must be released with
 * mnemosyne_sess_close().
 *
 * @return Session handle on success; NULL on allocation failure or if
 *         mnemosyne_engine_init() was not called yet.
 */
mnemosyne_session_t *mnemosyne_sess_new(void);

/**
 * Bind the session to a remote endpoint and complete the mnemosyne
 * handshake (TCP connect → HelloMode::CLIENT, protocol 2 → auth).
 *
 * Counterpart of frontend_sess_connect_by_addrstr_devid().  Signature is
 * deliberately a one-to-one mirror so that integrators can swap between
 * the two with mechanical regularity:
 *
 *     int frontend_sess_connect_by_addrstr_devid(
 *         struct FrontendSession *sess, int proto,
 *         const char *addr_str, uint16_t dev_id);
 *
 *     int mnemosyne_sess_connect_by_addrstr_devid(
 *         mnemosyne_session_t *sess, int proto,
 *         const char *addr_str, uint16_t dev_id);
 *
 * If `role == MNEMOSYNE_ROLE_ALLOC` (set at engine init time) the
 * session also issues a tryAlloc to create the shared 4 KiB block;
 * if `role == MNEMOSYNE_ROLE_REF` the session issues refBlock against
 * the predictable access key.
 *
 * @param sess     Session created by mnemosyne_sess_new().
 * @param proto    MNEMOSYNE_PROTO_TCP (1) or MNEMOSYNE_PROTO_UDP (0).
 *                 monkey-mnemosyne only uses TCP at present; UDP is
 *                 accepted at the API surface but rejected internally.
 * @param addr_str "IP:PORT" string, e.g. "10.0.0.5:10100".  Same format
 *                 as front's API.  May be NULL/empty to reuse the IP and
 *                 port given to mnemosyne_engine_init().
 * @param dev_id   Backend NIC selector.  MNEMOSYNE_DEV_AUTO (0xFF) lets
 *                 the backend pick; pass an explicit id for multi-NIC
 *                 deployments.  Stored in SessIPv4Params.device_selection
 *                 and forwarded to the backend verbatim.
 *
 * @return 0 on success; <0 on protocol / network error.
 */
int mnemosyne_sess_connect_by_addrstr_devid(mnemosyne_session_t *sess,
                                            int         proto,
                                            const char *addr_str,
                                            uint16_t    dev_id);

/**
 * Write up to MNEMOSYNE_BLOCK_SIZE bytes into the shared 4 KiB block.
 *
 * The buffer is copied into an internal staging page (zero-padded if
 * `size < MNEMOSYNE_BLOCK_SIZE`) and then writeBlock is issued to the
 * server.
 *
 * @return Number of bytes consumed from `data` (== `size` on success);
 *         <0 on error (e.g. size > MNEMOSYNE_BLOCK_SIZE, no session).
 */
int mnemosyne_sess_send(mnemosyne_session_t *sess,
                        const void *data, uint32_t size);

/**
 * Read the current contents of the shared 4 KiB block and copy up to
 * `size` bytes into `buf`.
 *
 * This is a single readBlock (not a polling-until-changed loop); the
 * upper-layer application is expected to call mnemosyne_sess_recv()
 * repeatedly inside its own loop and detect updates itself, exactly as
 * lab-main.cc::waitDataUpdate() does.
 *
 * @return Number of bytes copied into `buf` (== min(size, 4096) on
 *         success); <0 on error.
 */
int mnemosyne_sess_recv(mnemosyne_session_t *sess,
                        void *buf, uint32_t size);


int mnemosyne_sess_send_plain_text(mnemosyne_session_t *sess, const char *text, int64_t* checksum);

/**
 * Close the session and release all resources.
 *
 * If this endpoint holds a reference to the shared block it is unref'd
 * before the underlying TCP connection is torn down.  After this call
 * `sess` is invalid and must not be used.
 *
 * Safe to call with NULL.
 */
void mnemosyne_sess_close(mnemosyne_session_t *sess);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MNEMOSYNE_API_H */
