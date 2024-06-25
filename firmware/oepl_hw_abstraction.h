#ifndef OEPL_HW_ABSTRACTION_H
#define OEPL_HW_ABSTRACTION_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#ifndef GLOBAL_DEBUG_ENABLE
#define GLOBAL_DEBUG_ENABLE 1
#endif

typedef enum {
  BUTTON_1,
  BUTTON_2,
  GENERIC_GPIO,
  NFC_WAKE
} oepl_hw_gpio_channel_t;

typedef enum {
  RISING,
  FALLING
} oepl_hw_gpio_event_t;

typedef enum {
  DBG_APP,
  DBG_HW,
  DBG_RADIO,
  DBG_GPIO,
  DBG_LED,
  DBG_DISPLAY,
  DBG_NVM,
  DBG_FLASH,
  DBG_OTHER
} oepl_hw_debug_module_t;

typedef void (*oepl_hw_gpio_cb_t)(oepl_hw_gpio_channel_t button, oepl_hw_gpio_event_t event);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
void oepl_hw_init(void);

// ----------------------- LED Control -----------------------------------------
void oepl_hw_set_led(uint8_t color, bool on);

// ----------------------- GPIO Interrupts -------------------------------------
void oepl_hw_init_gpio(oepl_hw_gpio_cb_t cb);

// ----------------------- ADC control -----------------------------------------
bool oepl_hw_get_temperature(int8_t* temperature_degc);
bool oepl_hw_get_voltage(uint16_t* voltage_mv, bool force_measurement);

// ----------------------- NFC control -----------------------------------------
bool oepl_hw_nfc_write_url(const uint8_t* url_buffer, size_t length);
bool oepl_hw_nfc_write_raw(const uint8_t* raw_buffer, size_t length);

// ----------------------- HW/SW identification --------------------------------
uint8_t oepl_hw_get_hwid(void);
uint8_t oepl_hw_get_capabilities(void);
uint16_t oepl_hw_get_swversion(void);
const char* oepl_hw_get_swsuffix(void);
bool oepl_hw_get_screen_properties(size_t* x, size_t* y, size_t* bpp);
bool oepl_hw_get_screen_controller(uint8_t* controller_type);

// ----------------------- Deepsleep -------------------------------------------
void oepl_hw_enter_deepsleep(void);

// ----------------------- External flash --------------------------------------
void oepl_hw_flash_deepsleep(void);
void oepl_hw_flash_wake(void);

// ----------------------- Crash / debug ---------------------------------------
void oepl_hw_reboot(void);
void oepl_hw_crash(oepl_hw_debug_module_t module, bool reboot, const char* fmt, ...);

#if GLOBAL_DEBUG_ENABLE
void oepl_hw_debugprint(oepl_hw_debug_module_t module, const char* fmt, ...);
#else
#define oepl_hw_debugprint(x, y, ...)
#endif

#endif // OEPL_HW_ABSTRACTION_H