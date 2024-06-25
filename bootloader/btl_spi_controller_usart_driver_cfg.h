/***************************************************************************//**
 * @file
 * @brief Configuration header for bootloader Spi Controller Usart Driver
 *******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#ifndef BTL_SPI_CONTROLLER_USART_DRIVER_CONFIG_H
#define BTL_SPI_CONTROLLER_USART_DRIVER_CONFIG_H

#include "bootloader_pinout.h"

// <<< Use Configuration Wizard in Context Menu >>>

// <h>SPI Controller USART Driver

// <o SL_USART_EXTFLASH_FREQUENCY> Frequency
// <i> Default: 6400000
#define SL_USART_EXTFLASH_FREQUENCY           6400000

// </h>

// <<< end of configuration section >>>

// <<< sl:start pin_tool >>>
// <usart signal=TX,RX,CLK,(CS)> SL_USART_EXTFLASH
// $[USART_SL_USART_EXTFLASH]
#ifndef SL_USART_EXTFLASH_PERIPHERAL            
#define SL_USART_EXTFLASH_PERIPHERAL             USART0
#endif
#ifndef SL_USART_EXTFLASH_PERIPHERAL_NO         
#define SL_USART_EXTFLASH_PERIPHERAL_NO          0
#endif

// USART0 TX on PC00
#ifndef SL_USART_EXTFLASH_TX_PORT               
#define SL_USART_EXTFLASH_TX_PORT                gpioPortC
#endif
#ifndef SL_USART_EXTFLASH_TX_PIN                
#define SL_USART_EXTFLASH_TX_PIN                 0
#endif

// USART0 RX on PC01
#ifndef SL_USART_EXTFLASH_RX_PORT               
#define SL_USART_EXTFLASH_RX_PORT                gpioPortC
#endif
#ifndef SL_USART_EXTFLASH_RX_PIN                
#define SL_USART_EXTFLASH_RX_PIN                 1
#endif

// USART0 CLK on PC02
#ifndef SL_USART_EXTFLASH_CLK_PORT              
#define SL_USART_EXTFLASH_CLK_PORT               gpioPortC
#endif
#ifndef SL_USART_EXTFLASH_CLK_PIN               
#define SL_USART_EXTFLASH_CLK_PIN                2
#endif

// USART0 CS on PA04
#ifndef SL_USART_EXTFLASH_CS_PORT               
#define SL_USART_EXTFLASH_CS_PORT                gpioPortA
#endif
#ifndef SL_USART_EXTFLASH_CS_PIN                
#define SL_USART_EXTFLASH_CS_PIN                 4
#endif
// [USART_SL_USART_EXTFLASH]$

// <gpio> SL_EXTFLASH_WP
// $[GPIO_SL_EXTFLASH_WP]

// [GPIO_SL_EXTFLASH_WP]$

// <gpio> SL_EXTFLASH_HOLD
// $[GPIO_SL_EXTFLASH_HOLD]

// [GPIO_SL_EXTFLASH_HOLD]$

// <<< sl:end pin_tool >>>

#endif // BTL_SPI_CONTROLLER_USART_DRIVER_CONFIG_H
