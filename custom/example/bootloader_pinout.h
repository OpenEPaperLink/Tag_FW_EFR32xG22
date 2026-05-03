#ifndef BOOTLOADER_PINOUT_H
#define BOOTLOADER_PINOUT_H
/******************************************************************************
 * This file contains the hardware definitions for the various targets for
 * which we can build the SPIF bootloader, since different hardware vendors
 * have chosen different pins.
 *****************************************************************************/
#define SL_USART_EXTFLASH_PERIPHERAL             USART0
#define SL_USART_EXTFLASH_PERIPHERAL_NO          0

#define SL_USART_EXTFLASH_TX_PORT                gpioPortC
#define SL_USART_EXTFLASH_TX_PIN                 1

#define SL_USART_EXTFLASH_RX_PORT                gpioPortC
#define SL_USART_EXTFLASH_RX_PIN                 0

#define SL_USART_EXTFLASH_CLK_PORT               gpioPortC
#define SL_USART_EXTFLASH_CLK_PIN                2

#define SL_USART_EXTFLASH_CS_PORT                gpioPortC
#define SL_USART_EXTFLASH_CS_PIN                 3

#endif // BOOTLOADER_PINOUT