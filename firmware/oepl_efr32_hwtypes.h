/******************************************************************************
 * This file contains the mapping between the tag hardware type used internally
 * in the EFR32-based OEPL firmware, and the 'hwtype' used in the OEPL
 * implementation.
 * 
 * The distinction is made to support folding support for multiple hardware
 * layouts using the same display type and resolution into one OEPL hwtype,
 * which would help mitigate value exhaustion in that single byte. The crux
 * being that all hardware under the same OEPL hwtype gets presented with the
 * same OTA file, so we can combine implementations using the same Silabs
 * IC family (e.g. EFR32xG22) and the same screen type, and abstract away the
 * PCB layout / screen driver differences internally in the firmware.
 * 
 * Currently known abstraction candidates:
 * - EFR32xG22:
 *   - Devkit hardware
 *   - SoluM devices with FCC ID 2AFWN-EL{xxx}F{y}{zzz}, where `xxx` is the tag
 *     diagonal screen size, `y` is either 3, 4, 5 or 6, and `zzz` seems to be
 *     some kind of configuration information (might be related to things like
 *     case color, buttons/no buttons, etc)
 *     - E.g. EL029F{3,4,5,6}{zzz} are all EFR32BG22 based, but have subtly
 *       different configurations. Since the FCC ID of the hardware is known at
 *       time of flashing, we can hardcode the hardware configuration in the
 *       bootloader (which won't get OTA'ed), and look at that value in the
 *       application runtime for the application to see which pinout to use,
 *       what features to enable, and which screen driver to use.
 *   - Digi International devices, FCC IDs SUFIFT24PL4A and SUFIFT27PL4A
 *****************************************************************************/
#ifndef OEPL_EFR32_HWTYPES_H
#define OEPL_EFR32_HWTYPES_H

#include "em_device.h"
#include "em_gpio.h"
#include "em_cmu.h"
#include "stddef.h"

#define gpioPortInvalid ((GPIO_Port_TypeDef)0xFF)

#define OEPL_EFR32XG22_HWTYPE_BRD4402B_WSTK             (0x01)
#define OEPL_EFR32XG22_HWTYPE_BRD4402B_WSTK_EPD         (0x02)
#define OEPL_EFR32XG22_HWTYPE_SOLUM_AUTODETECT          (0x03)
#define OEPL_EFR32XG22_HWTYPE_DISPLAYDATA_SUFIFT24PL4A  (0x04)
#define OEPL_EFR32XG22_HWTYPE_DISPLAYDATA_SUFIFT27PL4A  (0x05)
#define OEPL_EFR32XG22_HWTYPE_CUSTOM_9_7                (0x06)
#define OEPL_EFR32XG22_HWTYPE_MODCHIP_HD150             (0x07)
// ----- Add new HW types here and keep in sync with bootloader ----

// ----- ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ----
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_0             (0xF0)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_1             (0xF1)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_2             (0xF2)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_3             (0xF3)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_4             (0xF4)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_5             (0xF5)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_6             (0xF6)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_7             (0xF7)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_8             (0xF8)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_9             (0xF9)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_A             (0xFA)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_B             (0xFB)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_C             (0xFC)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_D             (0xFD)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_E             (0xFE)
#define OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_F             (0xFF)

typedef enum {
  BRD4402B_WSTK             = OEPL_EFR32XG22_HWTYPE_BRD4402B_WSTK,    
  BRD4402B_WSTK_EPD         = OEPL_EFR32XG22_HWTYPE_BRD4402B_WSTK_EPD,
  SOLUM_AUTODETECT          = OEPL_EFR32XG22_HWTYPE_SOLUM_AUTODETECT, 
  DISPLAYDATA_SUFIFT24PL4A  = OEPL_EFR32XG22_HWTYPE_DISPLAYDATA_SUFIFT24PL4A, 
  DISPLAYDATA_SUFIFT27PL4A  = OEPL_EFR32XG22_HWTYPE_DISPLAYDATA_SUFIFT27PL4A,
  CUSTOM_9_7                = OEPL_EFR32XG22_HWTYPE_CUSTOM_9_7,
  MODCHIP_HD150             = OEPL_EFR32XG22_HWTYPE_MODCHIP_HD150,
  // ----- Add new HW types here and keep in sync with bootloader ----

  // ----- ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ----
  HWTYPE_DEVELOPMENT_0 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_0,
  HWTYPE_DEVELOPMENT_1 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_1,
  HWTYPE_DEVELOPMENT_2 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_2,
  HWTYPE_DEVELOPMENT_3 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_3,
  HWTYPE_DEVELOPMENT_4 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_4,
  HWTYPE_DEVELOPMENT_5 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_5,
  HWTYPE_DEVELOPMENT_6 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_6,
  HWTYPE_DEVELOPMENT_7 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_7,
  HWTYPE_DEVELOPMENT_8 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_8,
  HWTYPE_DEVELOPMENT_9 = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_9,
  HWTYPE_DEVELOPMENT_A = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_A,
  HWTYPE_DEVELOPMENT_B = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_B,
  HWTYPE_DEVELOPMENT_C = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_C,
  HWTYPE_DEVELOPMENT_D = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_D,
  HWTYPE_DEVELOPMENT_E = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_E,
  HWTYPE_DEVELOPMENT_F = OEPL_EFR32XG22_HWTYPE_DEVELOPMENT_F,
} oepl_efr32xg22_hwtype_t;

typedef enum {
  WSTK_MEMLCD,
  EPD_SOLUM_AUTODETECT,
  EPD_SEEED_264_176_BWR,
  EPD_HD150,
} oepl_efr32xg22_displaytype_t;

typedef struct {
  GPIO_Port_TypeDef port;
  uint8_t pin : 4;
  uint8_t idle_state : 1;
} oepl_efr32xg22_gpio_t;

typedef struct {
  USART_TypeDef * usart;
  oepl_efr32xg22_gpio_t MOSI;
  oepl_efr32xg22_gpio_t MISO;
  oepl_efr32xg22_gpio_t SCK;
  oepl_efr32xg22_gpio_t nCS;
  oepl_efr32xg22_gpio_t EN;
} oepl_efr32xg22_flashconfig_t;

typedef struct {
  USART_TypeDef * usart;
  CMU_Clock_TypeDef usart_clock;
  oepl_efr32xg22_gpio_t MOSI;
  oepl_efr32xg22_gpio_t MISO;
  oepl_efr32xg22_gpio_t SCK;
  oepl_efr32xg22_gpio_t nCS;
  oepl_efr32xg22_gpio_t nCS2;
  oepl_efr32xg22_gpio_t DC;
  oepl_efr32xg22_gpio_t BUSY;
  oepl_efr32xg22_gpio_t nRST;
  oepl_efr32xg22_gpio_t enable;
  oepl_efr32xg22_displaytype_t type;
} oepl_efr32xg22_displayconfig_t;

typedef struct {
  oepl_efr32xg22_gpio_t gpio;
  oepl_efr32xg22_gpio_t nfc_fd;
  uint32_t nfc_fd_em4wuval;
  oepl_efr32xg22_gpio_t button1;
  uint32_t button1_em4wuval;
  oepl_efr32xg22_gpio_t button2;
  uint32_t button2_em4wuval;
} oepl_efr32xg22_pinconfig_t;

typedef struct {
  oepl_efr32xg22_gpio_t red;
  oepl_efr32xg22_gpio_t green;
  oepl_efr32xg22_gpio_t blue;
  oepl_efr32xg22_gpio_t white;
} oepl_efr32xg22_ledconfig_t;

typedef struct {
  I2C_TypeDef * i2c;
  oepl_efr32xg22_gpio_t SDA;
  oepl_efr32xg22_gpio_t SCL;
  oepl_efr32xg22_gpio_t power;
} oepl_efr32xg22_nfcconfig_t;

typedef enum {
  // For the purposes of debug print, EUART is just as capable as USART
  // Regular 115200 baud, 8-n-1
  DBG_EUART,
  // SWO can be an alternative when the SWO pin is available on the hardware, but no full UART.
  DBG_SWO,
  // RTT is nice since it doesn't require extra IO, but it does require RAM buffers. Additionally,
  // being connected keeps the debug circuitry alive, which messes with measuring power consumption.
  DBG_RTT
} oepl_efr32xg22_debug_t;

typedef struct {
  union {
    struct {
      oepl_efr32xg22_gpio_t tx;
      oepl_efr32xg22_gpio_t rx;
      oepl_efr32xg22_gpio_t cts;
      oepl_efr32xg22_gpio_t rts;
      oepl_efr32xg22_gpio_t enable;
    } euart;
  } output;
  oepl_efr32xg22_debug_t type;
} oepl_efr32xg22_debugconfig_t;

typedef struct {
  oepl_efr32xg22_hwtype_t hwtype;
  uint8_t oepl_hwid;
  const oepl_efr32xg22_flashconfig_t* flash;
  const oepl_efr32xg22_displayconfig_t* display;
  const oepl_efr32xg22_pinconfig_t* gpio;
  const oepl_efr32xg22_ledconfig_t* led;
  const oepl_efr32xg22_nfcconfig_t* nfc;
  const oepl_efr32xg22_debugconfig_t* debug;
} oepl_efr32xg22_tagconfig_t;

typedef enum {
  CTRL_MEMLCD,
  CTRL_UC8179,
  CTRL_UC8159,
  CTRL_EPDVAR26,
  CTRL_EPDVAR29,
  CTRL_EPDVAR43,
  CTRL_SSD,
  CTRL_DUALSSD,
  CTRL_IL91874,
  CTRL_GDEW0583Z83,
  CTRL_UCBWRY,
  CTRL_JD,
  CTRL_INTERLEAVED,
  // ----- Add new display driver types here ----

  // ----- ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ----
  CTRL_DEVELOPMENT,
} oepl_efr32xg22_displaydriver_t;

typedef struct {
  size_t xres;
  size_t yres;
  size_t xres_working;
  size_t yres_working;
  size_t xoffset;
  size_t yoffset;
  bool have_thirdcolor;
  bool have_fourthcolor;
  bool swapXY;
  bool mirrorX;
  bool mirrorY;
  oepl_efr32xg22_displaydriver_t ctrl;
} oepl_efr32xg22_displayparams_t;

// Get the config structure for the hardware we're running on.
const oepl_efr32xg22_tagconfig_t* oepl_efr32xg22_get_config(void);

// Get the OEPL HWID for the hardware we're running on
// This includes mapping of Solum userdata info to OEPL hwids
uint8_t oepl_efr32xg22_get_oepl_hwid(void);

// Get the OEPL HW capability mask for the hardware we are
// running on (autodetection for Solum)
uint8_t oepl_efr32xg22_get_oepl_hwcapa(void);

// Get the EPD display parameters to use for this hardware
// Returns false if no information available
bool oepl_efr32xg22_get_displayparams(oepl_efr32xg22_displayparams_t* displayparams);

#endif //OEPL_EFR32_HWTYPES_H