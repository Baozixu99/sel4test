/*
 * Copyright 2025, seL4 Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

/* Machine-specific serial definitions for Phytium */

/* PL011 UART register offsets */
#define UART_DR         0x00    /* Data register */
#define UART_RSR        0x04    /* Receive status register */
#define UART_FR         0x18    /* Flag register */
#define UART_IBRD       0x24    /* Integer baud rate divisor */
#define UART_FBRD       0x28    /* Fractional baud rate divisor */
#define UART_LCRH       0x2C    /* Line control register */
#define UART_CR         0x30    /* Control register */
#define UART_IMSC       0x38    /* Interrupt mask set/clear register */
#define UART_ICR        0x44    /* Interrupt clear register */

/* PL011 specific definitions */
#define PL011_UART_FREQ  48000000   /* 48MHz */
#define PL011_UART_CLK   PL011_UART_FREQ
