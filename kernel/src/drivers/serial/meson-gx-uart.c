/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */

#include <config.h>
#include <stdint.h>
#include <string.h>
#include <util.h>
#include <machine/io.h>
#include <plat/machine/devices_gen.h>

#define UART_WFIFO  0x0
#define UART_RFIFO  0x4
#define UART_CTRL   0x8
#define UART_STATUS 0xC
#define UART_MISC   0x10

#define UART_TX_FULL        BIT(21)
#define UART_RX_EMPTY       BIT(20)

#define UART_RX_IRQ         BIT(27)
#define UART_RX_EN          BIT(13)
#define UART_TX_EN          BIT(12)

#define UART_REG(x) ((volatile uint32_t *)(UART_PPTR + (x)))

#define WDOG_EN BIT(18)
#define WDOG_SYS_RESET_EN BIT(21)
#define WDOG_CLK_EN BIT(24)
#define WDOG_CLK_DIV_EN BIT(25)
#define WDOG_SYS_RESET_NOW BIT(26)

static const char *reset = "reset";
static int index = 0;

void putDebugChar(unsigned char c);

void init_serial(void)
{
    /* enable tx, rx and rx irq */
    *(UART_REG(UART_CTRL)) |= UART_TX_EN | UART_RX_EN | UART_RX_IRQ;
    /* send irq when 1 char is available */
    *(UART_REG(UART_MISC)) = 1;
}

void handleUartIRQ(void)
{
    /* while there are chars to process */
    while (!(*UART_REG(UART_STATUS) & UART_RX_EMPTY)) {
        char c = *UART_REG(UART_RFIFO);
        putDebugChar(c);
        if (c == 'r') {
            index = 1;
        } else if (c == reset[index]) {
            index++;
        } else {
            index = 0;
        }
        if (index == strnlen(reset, 5)) {
            /* do the reset */
            printf("\nResetting Odroid-C2\n");
            volatile uint32_t *wdog = (volatile uint32_t *) (WDOG_PPTR);
            *wdog = (WDOG_EN | WDOG_SYS_RESET_EN | WDOG_CLK_EN |
                     WDOG_CLK_DIV_EN | WDOG_SYS_RESET_NOW);
        }
    }
}

void putDebugChar(unsigned char c)
{
    while ((*UART_REG(UART_STATUS) & UART_TX_FULL));

    /* Add character to the buffer. */
    *UART_REG(UART_WFIFO) = c;
}

#ifdef CONFIG_DEBUG_BUILD
unsigned char getDebugChar(void)
{
    while ((*UART_REG(UART_STATUS) & UART_RX_EMPTY));
    return *UART_REG(UART_RFIFO);
}
#endif /* CONFIG_DEBUG_BUILD */
