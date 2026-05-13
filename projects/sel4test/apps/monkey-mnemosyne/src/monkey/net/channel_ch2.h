/**
 * @file channel_ch2.h
 * @brief HyperAMP channel-2 layout constants for monkey-mnemosyne.
 *
 * Background
 * ----------
 *   Per HYPERAMP_MULTI_CHANNEL_DESIGN.md (top of repo) and
 *   apps/hyperamp-server/src/{channel.c,main.c}, the seL4 boot loader
 *   maps a single 4 MiB window of shared memory and publishes the per-
 *   channel virtual addresses through IPC message registers:
 *
 *       msg[2..4]  -> CH0  (TX, RX, Data)   -- hyperamp-server
 *       msg[5..7]  -> CH1  (TX, RX, Data)   -- front / HighSpeedCProxy
 *       msg[8..10] -> CH2  (TX, RX, Data)   -- monkey-mnemosyne (us)
 *
 *   Physical addresses are a fixed offset from a per-platform base:
 *
 *       CH0 base = base + 0x000000
 *       CH1 base = base + 0x200000
 *       CH2 base = base + 0x300000
 *
 *   The virtual addresses are NOT compile-time constants; they are
 *   computed at boot by the kernel and must be read from the IPC MRs
 *   *before any seL4 syscall is issued*, otherwise the IPC buffer is
 *   overwritten and the values are lost.  See mnemosyne_api.cc for the
 *   single authoritative reader.
 *
 *   This header therefore exposes only the bits that ARE compile-time
 *   constant: the MR slot indices, the physical address offsets, and
 *   the queue capacity.
 *
 * Why a separate header
 * ---------------------
 *   Mirrors the apps/hyperamp-server/include/channel_ch1.h pattern
 *   (one tiny header per channel) so that integrators can cross-
 *   reference the two cleanly when wiring up a unified application.
 *
 * gongty [at] tongji [dot] edu [dot] cn
 */

#ifndef MONKEY_NET_CHANNEL_CH2_H
#define MONKEY_NET_CHANNEL_CH2_H

/* ---- IPC message-register slots ---------------------------------------- */
/*
 * NOTE: seL4 message registers are read with seL4_GetMR(i).  The kernel
 * boot loader writes ipcBuf[i + 1] = vaddr, hence MR_SLOT_CH2_TX = 8
 * corresponds to seL4_GetMR(8) returning the CH2 TX queue vaddr.
 */
#define MNEMOSYNE_MR_SLOT_CH2_TX     8
#define MNEMOSYNE_MR_SLOT_CH2_RX     9
#define MNEMOSYNE_MR_SLOT_CH2_DATA  10

/* ---- Physical address offsets (relative to the platform shm base) ------ */
/*
 * The platform shm base lives in hyperamp_shm_queue.h as
 * SHM_TX_QUEUE_PADDR (defined per CONFIG_PLAT_*).  We derive the CH2
 * physical addresses from that base + this offset, exactly the way
 * apps/front/src/engine.c derives CH1 from base + HYPERAMP_CH1_OFFSET_PADDR.
 */
#define MNEMOSYNE_CH2_PADDR_OFFSET     0x300000UL
#define MNEMOSYNE_CH2_TX_PADDR_OFFSET  (MNEMOSYNE_CH2_PADDR_OFFSET + 0x000UL)
#define MNEMOSYNE_CH2_RX_PADDR_OFFSET  (MNEMOSYNE_CH2_PADDR_OFFSET + 0x1000UL)

/* ---- Queue capacity ---------------------------------------------------- */
/*
 * Same as apps/hyperamp-server CH1 (253):
 *   total CH2 area  = 1 MiB
 *   minus 2 control pages (TX queue + RX queue header), one block each
 *   data region     = 1 MiB - 8 KiB = 254 * 4 KiB blocks
 *   enqueue uses (idx + 1) * block_size, so capacity must be <= 253.
 *
 * monkey-mnemosyne actually only needs a single 4 KiB block (one shared
 * page); 253 is intentionally generous to leave headroom and to keep
 * the layout consistent with CH1 when the integrator visualises it.
 */
#define MNEMOSYNE_CH2_QUEUE_CAPACITY  253

#endif /* MONKEY_NET_CHANNEL_CH2_H */
