/*
 * Copyright 2024 seL4 Project a Series of LF Projects, LLC.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <platsupport/serial.h>

int pl011_uart_init(const struct dev_defn *defn, const ps_io_ops_t *ops, ps_chardevice_t *dev,
                    uint32_t freq_uart_clk);
int pl011_uart_getchar(ps_chardevice_t *d);
int pl011_uart_putchar(ps_chardevice_t *d, int c);
