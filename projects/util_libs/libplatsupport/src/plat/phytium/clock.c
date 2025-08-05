/*
 * Copyright 2025, seL4 Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <platsupport/clock.h>
#include <platsupport/plat/clock.h>

/* Phytium Pi clock management - simplified implementation */

static freq_t
phytium_get_freq(clk_t* clk)
{
    switch (clk->id) {
    case CLK_UART:
        return UART_REF_CLK;
    case CLK_ARM:
        return ARM_CLK_FREQ;
    default:
        return 0;
    }
}

static freq_t
phytium_set_freq(clk_t* clk, freq_t hz)
{
    /* Most clocks on Phytium Pi are fixed frequency */
    return phytium_get_freq(clk);
}

static void
phytium_recal(clk_t* clk UNUSED)
{
    /* Nothing to recalculate for fixed clocks */
}

static clk_t*
phytium_init(clk_t* clk)
{
    if (clk == NULL) {
        return NULL;
    }
    clk->recal = &phytium_recal;
    clk->get_freq = &phytium_get_freq;
    clk->set_freq = &phytium_set_freq;
    clk->recal(clk);
    return clk;
}

int
clock_sys_init(ps_io_ops_t* io_ops UNUSED, clock_sys_t* clock_sys)
{
    if (clock_sys == NULL) {
        return -1;
    }

    clock_sys->priv = NULL;
    clock_sys->get_clock = NULL;  /* Simplified - no dynamic clock getting */
    clock_sys->gate_enable = NULL;

    return 0;
}
