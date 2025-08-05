/*
 * Copyright 2025, seL4 Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Phytium Pi clock frequencies (based on PE2204 official DTS)
#define ARM_CLK_FREQ    1800000000UL  // 1.8GHz (estimated, not specified in DTS)
#define UART_REF_CLK    48000000UL    // 48MHz (0x2dc6c00 from DTS)
#define SYS_CLK_FREQ    50000000UL    // 50MHz (0x2faf080 from DTS)

enum clk_id {
    CLK_MASTER,
    CLK_ARM,
    CLK_UART,
    CLK_SYS,
    NCLOCKS,
};

enum clock_gate {
    NCLKGATES
};
