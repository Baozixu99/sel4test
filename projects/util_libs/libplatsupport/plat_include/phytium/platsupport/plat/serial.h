/*
 * Copyright 2025, seL4 Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <autoconf.h>

/* Phytium Pi character device identifiers */
enum chardev_id {
    PHYTIUM_UART0,
    PHYTIUM_UART1,
    PHYTIUM_UART2,
    PHYTIUM_UART3,
    /* Aliases */
    PS_SERIAL0 = PHYTIUM_UART0,
    PS_SERIAL1 = PHYTIUM_UART1,
    PS_SERIAL2 = PHYTIUM_UART2,
    PS_SERIAL3 = PHYTIUM_UART3,
    /* defaults */
    PS_SERIAL_DEFAULT = PHYTIUM_UART1
};

// Phytium Pi UART addresses (based on PE2204 SoC official DTS)
#define UART0_PADDR  0x2800c000  /* Currently disabled in DTS */
#define UART1_PADDR  0x2800d000  /* Active serial console */
#define UART2_PADDR  0x2800e000  /* Currently disabled in DTS */
#define UART3_PADDR  0x2800f000  /* Currently disabled in DTS */

// Phytium Pi UART interrupts (from official DTS)
#define UART0_IRQ    83  // 0x53 GIC SPI
#define UART1_IRQ    84  // 0x54 GIC SPI  
#define UART2_IRQ    85  // 0x55 GIC SPI
#define UART3_IRQ    86  // 0x56 GIC SPI

// UART reference clock (48MHz from official DTS) - defined in clock.h
/* #define UART_REF_CLK 48000000 */

// Default serial configuration (UART1 for console per DTS aliases)
#define DEFAULT_SERIAL_PADDR        UART1_PADDR
#define DEFAULT_SERIAL_INTERRUPT    UART1_IRQ
