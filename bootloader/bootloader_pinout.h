#ifndef BOOTLOADER_PINOUT_H
#define BOOTLOADER_PINOUT_H
/******************************************************************************
 * This file contains the hardware definitions for the various targets for
 * which we can build the SPIF bootloader, since different hardware vendors
 * have chosen different pins.
 *****************************************************************************/
#if BTL_TYPE_BRD4402B
#define SL_USART_EXTFLASH_PERIPHERAL             USART0
#define SL_USART_EXTFLASH_PERIPHERAL_NO          0

#define SL_USART_EXTFLASH_TX_PORT                gpioPortC
#define SL_USART_EXTFLASH_TX_PIN                 0

#define SL_USART_EXTFLASH_RX_PORT                gpioPortC
#define SL_USART_EXTFLASH_RX_PIN                 1

#define SL_USART_EXTFLASH_CLK_PORT               gpioPortC
#define SL_USART_EXTFLASH_CLK_PIN                2

#define SL_USART_EXTFLASH_CS_PORT                gpioPortA
#define SL_USART_EXTFLASH_CS_PIN                 4
#elif BTL_TYPE_SOLUM
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
#elif BTL_TYPE_DISPLAYDATA
#define SL_USART_EXTFLASH_PERIPHERAL             USART0
#define SL_USART_EXTFLASH_PERIPHERAL_NO          0

#define SL_USART_EXTFLASH_TX_PORT                gpioPortC
#define SL_USART_EXTFLASH_TX_PIN                 0

#define SL_USART_EXTFLASH_RX_PORT                gpioPortC
#define SL_USART_EXTFLASH_RX_PIN                 1

#define SL_USART_EXTFLASH_CLK_PORT               gpioPortC
#define SL_USART_EXTFLASH_CLK_PIN                2

#define SL_USART_EXTFLASH_CS_PORT                gpioPortC
#define SL_USART_EXTFLASH_CS_PIN                 3
#elif BTL_TYPE_CUSTOM
#define SL_USART_EXTFLASH_PERIPHERAL             USART1
#define SL_USART_EXTFLASH_PERIPHERAL_NO          1

#define SL_USART_EXTFLASH_TX_PORT                gpioPortB
#define SL_USART_EXTFLASH_TX_PIN                 0

#define SL_USART_EXTFLASH_RX_PORT                gpioPortA
#define SL_USART_EXTFLASH_RX_PIN                 4

#define SL_USART_EXTFLASH_CLK_PORT               gpioPortA
#define SL_USART_EXTFLASH_CLK_PIN                0

#define SL_USART_EXTFLASH_CS_PORT                gpioPortA
#define SL_USART_EXTFLASH_CS_PIN                 5

#define SL_EXTFLASH_EN_PORT                      gpioPortA
#define SL_EXTFLASH_EN_PIN                       6
#elif BTL_TYPE_MODCHIP
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
// ----- Add new pinouts here and keep in sync with hwtypes in FW ----

// ----- ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ----
#elif BTL_TYPE_MANUAL
#define SL_USART_EXTFLASH_PERIPHERAL             BTL_EXTFLASH_PERIPHERAL
#define SL_USART_EXTFLASH_PERIPHERAL_NO          BTL_EXTFLASH_PERIPHERAL_NO
#define SL_USART_EXTFLASH_TX_PORT                BTL_EXTFLASH_TX_PORT
#define SL_USART_EXTFLASH_TX_PIN                 BTL_EXTFLASH_TX_PIN
#define SL_USART_EXTFLASH_RX_PORT                BTL_EXTFLASH_RX_PORT
#define SL_USART_EXTFLASH_RX_PIN                 BTL_EXTFLASH_RX_PIN
#define SL_USART_EXTFLASH_CLK_PORT               BTL_EXTFLASH_CLK_PORT
#define SL_USART_EXTFLASH_CLK_PIN                BTL_EXTFLASH_CLK_PIN
#define SL_USART_EXTFLASH_CS_PORT                BTL_EXTFLASH_CS_PORT
#define SL_USART_EXTFLASH_CS_PIN                 BTL_EXTFLASH_CS_PIN
#else
#error "Undefined target, please define the flag for which hardware configuration to generate"
#endif
#endif // BOOTLOADER_PINOUT