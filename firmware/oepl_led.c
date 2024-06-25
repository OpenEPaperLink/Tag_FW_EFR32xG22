// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_hw_abstraction.h"
#include "oepl_led.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_sleeptimer.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef LED_DEBUG_PRINT
#define LED_DEBUG_PRINT 1
#endif

// Mode 1 timing configuration
#define LED_MODE_1_MS_PER_INNER_LOOP (100)
#define LED_MODE_1_MS_PER_OUTER_LOOP (100)

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if LED_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_LED, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

typedef struct {
  uint8_t color;
  uint8_t inner_delay;
  uint8_t after_delay;
  uint8_t inner_count;
} led_mode1_group_t;

typedef struct {
  led_mode1_group_t group1;
  led_mode1_group_t group2;
  led_mode1_group_t group3;
  uint8_t repeats;
  uint8_t flash_duration;
  uint8_t loop_counter;
  uint8_t group_step;
} led_mode1_t;

typedef union {
  led_mode1_t mode1;
} led_mode_config_t;

typedef struct {
  led_mode_config_t config;
  bool active;
  uint8_t mode;
} led_data_t;

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
static void led_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static led_data_t current_sequence = {
  .mode = 0,
  .active = false
};
static sl_sleeptimer_timer_handle_t led_timer_handle;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
/**************************************************************************//**
 * Start an LED flash sequence if no sequence is currently running.
 * 
 * Input is the 12-byte LED control payload as defined in
 * https://github.com/jjwbruijn/OpenEPaperLink/wiki/Led-control
 *****************************************************************************/
bool oepl_led_flash_sequence(const uint8_t led_data[12])
{
  if(!current_sequence.active) {
    DPRINTF("Executing LED command: ");
    for(size_t i = 0; i< 12; i++) {
      DPRINTF("%02x", led_data[i]);
    }
    DPRINTF("\n");

    current_sequence.mode = led_data[0] & 0x0F;

    // Parse the contents of the data bytes into members of the sequence struct
    switch(current_sequence.mode) {
      case 1:
        // Mode 1 is what the standard APs send when right-clicking 'Flash LED'
        // 81 = mode 1, flash_duration 8ms
        // 3c = group 1 color 60
        // 13 = 100ms loop delay group 1, 3 iterations
        // 0a = 1s delay after group 1
        // e4 = group 2 color 228
        // 53 = 500ms loop delay group 2, 3 iterations
        // 0a = 1s delay after group 2
        // 03 = group 3 color 3
        // a3 = 1000ms loop delay group 3, 3 iterations
        // 0a = 1s delay after group 3
        // 02 = do it all twice
        // 00 = spare

        // Parse the content of led_data
        current_sequence.config.mode1.flash_duration = led_data[0] >> 4;

        current_sequence.config.mode1.group1.color = led_data[1];
        current_sequence.config.mode1.group2.color = led_data[4];
        current_sequence.config.mode1.group3.color = led_data[7];

        current_sequence.config.mode1.group1.inner_delay = led_data[2] >> 4;
        current_sequence.config.mode1.group2.inner_delay = led_data[5] >> 4;
        current_sequence.config.mode1.group3.inner_delay = led_data[8] >> 4;

        current_sequence.config.mode1.group1.inner_count = led_data[2] & 0x0F;
        current_sequence.config.mode1.group2.inner_count = led_data[5] & 0x0F;
        current_sequence.config.mode1.group3.inner_count = led_data[8] & 0x0F;

        current_sequence.config.mode1.group1.after_delay = led_data[3];
        current_sequence.config.mode1.group2.after_delay = led_data[6];
        current_sequence.config.mode1.group3.after_delay = led_data[9];

        current_sequence.config.mode1.repeats = led_data[10];

        // Initialize the state-tracking variables for this mode
        current_sequence.config.mode1.group_step = 0;
        current_sequence.config.mode1.loop_counter = 0;
        current_sequence.active = true;
        break;
      default:
        DPRINTF("Unknown mode %d, can't execute\n", current_sequence.mode);
        return false;
    }

    // Kick off the LED flash 'task' on a timer to not block the main event loop
    sl_sleeptimer_start_timer_ms(
      &led_timer_handle,
      1,
      led_timer_cb,
      NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
    return true;
  } else {
    // Previous sequence did not finish
    return false;
  }
}

/**************************************************************************//**
 * Abort the LED flash sequence (if one is currently ongoing). If no sequence
 * is in progress, this call is a no-op.
 *****************************************************************************/
void oepl_led_abort(void)
{
  sl_sleeptimer_stop_timer(&led_timer_handle);
  oepl_hw_set_led(0xFF, false);
  current_sequence.active = false;
}

// -----------------------------------------------------------------------------
//                          Static Function Definitions
// -----------------------------------------------------------------------------
static void led_timer_cb(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void) handle;
  (void) data;

  if(!current_sequence.active) {
    // Shouldn't have ended up here. Stop ourselves once more.
    sl_sleeptimer_stop_timer(&led_timer_handle);
    return;
  }

  switch(current_sequence.mode) {
    case 1:
      // group_step is used to track our progress within the overall sequence.
      // It is used as a bitmask, where
      // - bits 1:0 are the state within a group:
      //     - 0b00 = turn on LED and wait flash_duration
      //     - 0b01 = turn off LED and wait inner_delay
      //     - 0b10 = wait after_delay
      // - bits 3:2 are the group we're handling:
      //     - 0b00: group 1
      //     - 0b01: group 2
      //     - 0b10: group 3
      if((current_sequence.config.mode1.group_step & 0b11) == 0) {
        // Turn on LED and wait flash_duration
        if((current_sequence.config.mode1.group_step >> 2) == 0) {
          oepl_hw_set_led(current_sequence.config.mode1.group1.color, true);
        } else if((current_sequence.config.mode1.group_step >> 2) == 1) {
          oepl_hw_set_led(current_sequence.config.mode1.group2.color, true);
        } else if((current_sequence.config.mode1.group_step >> 2) == 2) {
          oepl_hw_set_led(current_sequence.config.mode1.group3.color, true);
        } else {
          DPRINTF("ERR: exited LED loop\n");
          current_sequence.active = false;
          return;
        }
        current_sequence.config.mode1.group_step += 1;
        sl_sleeptimer_start_timer_ms(
          &led_timer_handle,
          current_sequence.config.mode1.flash_duration,
          led_timer_cb,
          NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        return;
      } else if((current_sequence.config.mode1.group_step & 0b11) == 1) {
        // Turn off LED and wait inner_delay
        oepl_hw_set_led(0xFF, false);
        if((current_sequence.config.mode1.group_step >> 2) == 0) {
          if(current_sequence.config.mode1.loop_counter < current_sequence.config.mode1.group1.inner_count) {
            current_sequence.config.mode1.loop_counter++;
            current_sequence.config.mode1.group_step &= ~0b11;
          } else {
            current_sequence.config.mode1.loop_counter = 0;
            current_sequence.config.mode1.group_step += 1;
          }
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_INNER_LOOP * current_sequence.config.mode1.group1.inner_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else if((current_sequence.config.mode1.group_step >> 2) == 1) {
          if(current_sequence.config.mode1.loop_counter < current_sequence.config.mode1.group2.inner_count) {
            current_sequence.config.mode1.loop_counter++;
            current_sequence.config.mode1.group_step &= ~0b11;
          } else {
            current_sequence.config.mode1.loop_counter = 0;
            current_sequence.config.mode1.group_step += 1;
          }
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_INNER_LOOP * current_sequence.config.mode1.group2.inner_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else if((current_sequence.config.mode1.group_step >> 2) == 2) {
          if(current_sequence.config.mode1.loop_counter < current_sequence.config.mode1.group3.inner_count) {
            current_sequence.config.mode1.loop_counter++;
            current_sequence.config.mode1.group_step &= ~0b11;
          } else {
            current_sequence.config.mode1.loop_counter = 0;
            current_sequence.config.mode1.group_step += 1;
          }
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_INNER_LOOP * current_sequence.config.mode1.group3.inner_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else {
          DPRINTF("ERR: exited LED loop\n");
          current_sequence.active = false;
          return;
        }
        return;
      } else if((current_sequence.config.mode1.group_step & 0b11) == 2) {
        // Wait after_delay
        if((current_sequence.config.mode1.group_step >> 2) == 0) {
          current_sequence.config.mode1.group_step += 1 << 2;
          current_sequence.config.mode1.group_step &= ~0b11;
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_OUTER_LOOP * current_sequence.config.mode1.group1.after_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else if((current_sequence.config.mode1.group_step >> 2) == 1) {
          current_sequence.config.mode1.group_step += 1 << 2;
          current_sequence.config.mode1.group_step &= ~0b11;
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_OUTER_LOOP * current_sequence.config.mode1.group2.after_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else if((current_sequence.config.mode1.group_step >> 2) == 2) {
          if(current_sequence.config.mode1.repeats > 0) {
            current_sequence.config.mode1.repeats--;
            current_sequence.config.mode1.loop_counter = 0;
            current_sequence.config.mode1.group_step = 0;
          } else {
            DPRINTF("Done LED blinking\n");
            current_sequence.active = false;
            return;
          }
          sl_sleeptimer_start_timer_ms(
            &led_timer_handle,
            LED_MODE_1_MS_PER_OUTER_LOOP * current_sequence.config.mode1.group3.after_delay,
            led_timer_cb,
            NULL, 0, SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG);
        } else {
          DPRINTF("ERR: exited LED loop\n");
          current_sequence.active = false;
          return;
        }
        return;
      }
      DPRINTF("ERR: exited LED loop\n");
      current_sequence.active = false;
      return;
    default:
      DPRINTF("ERR: unsupported mode\n");
      current_sequence.active = false;
      return;
  }
}
