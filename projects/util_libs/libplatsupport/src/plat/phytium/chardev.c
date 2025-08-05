/*
 * Copyright 2025, seL4 Project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/**
 * Contains the definition for all character devices on Phytium Pi platform.
 * Uses PL011 UART implementation adapted from BCM driver.
 */

#include "../../chardev.h"
#include "../../common.h"
#include <string.h>
#include <utils/util.h>
#include <platsupport/plat/serial.h>
#include <platsupport/serial.h>

static const int uart0_irqs[] = {UART0_IRQ, -1};
static const int uart1_irqs[] = {UART1_IRQ, -1};
static const int uart2_irqs[] = {UART2_IRQ, -1};
static const int uart3_irqs[] = {UART3_IRQ, -1};

/* PL011 register definitions - copied from BCM implementation */
typedef volatile struct pl011_regs_s {
    uint32_t dr;            // 0x00: data register
    uint32_t rsrecr;        // 0x04: receive status/error clear register
    uint64_t unused0[2];    // 0x08
    uint32_t fr;            // 0x18: flag register
    uint32_t unused1;       // 0x1c
    uint32_t ilpr;          // 0x20: not in use
    uint32_t ibrd;          // 0x24: integer baud rate divisor
    uint32_t fbrd;          // 0x28: fractional baud rate divisor
    uint32_t lcrh;          // 0x2c: line control register
    uint32_t cr;            // 0x30: control register
    uint32_t ifls;          // 0x34: interrupt FIFO level select register
    uint32_t imsc;          // 0x38: interrupt mask set clear register
    uint32_t ris;           // 0x3c: raw interrupt status register
    uint32_t mis;           // 0x40: masked interrupt status register
    uint32_t icr;           // 0x44: interrupt clear register
    uint32_t dmacr;         // 0x48: DMA control register
} pl011_regs_t;

/* LCRH register */
#define LCRH_WLEN_8BIT  (3 << 5) // Word length
#define LCRH_FEN        BIT(4)   // Enable FIFOs
#define LCRH_STP2       BIT(3)   // Two stop bits select
#define LCRH_PEN        BIT(1)   // Parity enable

/* CR register */
#define CR_RXE          BIT(9) // Receive enable
#define CR_TXE          BIT(8) // Transmit enable
#define CR_UARTEN       BIT(0) // UART enable

/* FR register */
#define FR_TXFF         BIT(5) // Transmit FIFO full
#define FR_RXFE         BIT(4) // Receive FIFO empty
#define FR_BUSY         BIT(3) // UART busy

/* IMSC register */
#define IMSC_RXIM       BIT(4)

static void phytium_pl011_uart_handle_irq(ps_chardevice_t *dev)
{
    pl011_regs_t *r = (pl011_regs_t *)(dev->vaddr);
    r->icr = 0x7ff; // Clear all interrupts
}

static int phytium_pl011_uart_configure(ps_chardevice_t *dev)
{
    pl011_regs_t *r = (pl011_regs_t *)(dev->vaddr);
    
    // Disable UART
    r->cr &= ~CR_UARTEN;
    
    // Wait till UART is not busy
    while (r->fr & FR_BUSY);
    
    // Disable FIFO
    r->lcrh &= ~LCRH_FEN;
    
    // Configure: 8-bit, no parity, 1 stop bit
    uint32_t lcrh = r->lcrh;
    lcrh |= LCRH_WLEN_8BIT;
    lcrh &= ~LCRH_STP2;
    lcrh &= ~LCRH_PEN;
    r->lcrh = lcrh;
    
    // Set baud rate (48MHz / (16 * 115200) = 26.04)
    r->ibrd = 26;
    r->fbrd = 3; // (0.04 * 64 + 0.5) ≈ 3
    
    // Enable TX/RX
    r->cr |= CR_TXE | CR_RXE;
    
    // Enable UART
    r->cr |= CR_UARTEN;
    
    // Enable RX interrupt
    r->imsc |= IMSC_RXIM;
    
    return 0;
}

static int phytium_pl011_getchar(ps_chardevice_t *d)
{
    pl011_regs_t *r = (pl011_regs_t *)(d->vaddr);
    
    // Check if receive FIFO is not empty
    if (!(r->fr & FR_RXFE)) {
        return (int)(r->dr & MASK(8));
    }
    return EOF;
}

static int phytium_pl011_putchar(ps_chardevice_t *d, int c)
{
    if ((d->flags & SERIAL_AUTO_CR) && ((char)c == '\n')) {
        phytium_pl011_putchar(d, '\r');
    }
    
    pl011_regs_t *r = (pl011_regs_t *)(d->vaddr);
    
    // Wait till transmit FIFO is not full
    while (r->fr & FR_TXFF);
    
    r->dr = (c & MASK(8));
    return c;
}

/* Platform-specific UART functions expected by common serial code */
int uart_getchar(ps_chardevice_t *d)
{
    return phytium_pl011_getchar(d);
}

int uart_putchar(ps_chardevice_t *d, int c)
{
    return phytium_pl011_putchar(d, c);
}

/* Phytium Pi UART initialization */
int phytium_uart_init(const struct dev_defn *defn, const ps_io_ops_t *ops, ps_chardevice_t *dev)
{
    void *vaddr = chardev_map(defn, ops);
    memset(dev, 0, sizeof(*dev));
    if (vaddr == NULL) {
        return -1;
    }
    
    /* Set up device properties */
    dev->id         = defn->id;
    dev->vaddr      = vaddr;
    dev->read       = &uart_read;
    dev->write      = &uart_write;
    dev->handle_irq = &phytium_pl011_uart_handle_irq;
    dev->irqs       = defn->irqs;
    dev->ioops      = *ops;
    dev->flags      = SERIAL_AUTO_CR;
    
    return phytium_pl011_uart_configure(dev);
}

#define UART_DEFN(devid) {          \
    .id      = PHYTIUM_UART##devid, \
    .paddr   = UART##devid##_PADDR, \
    .size    = BIT(12),             \
    .irqs    = uart##devid##_irqs,  \
    .init_fn = &phytium_uart_init   \
}

static const struct dev_defn dev_defn[] = {
    UART_DEFN(0),
    UART_DEFN(1),
    UART_DEFN(2),
    UART_DEFN(3),
};

struct ps_chardevice*
ps_cdev_init(enum chardev_id id, const ps_io_ops_t* o, struct ps_chardevice* d) {
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(dev_defn); i++) {
        if (dev_defn[i].id == id) {
            return (dev_defn[i].init_fn(dev_defn + i, o, d)) ? NULL : d;
        }
    }
    return NULL;
}
