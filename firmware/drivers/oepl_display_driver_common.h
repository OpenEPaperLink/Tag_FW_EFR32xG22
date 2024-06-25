#ifndef OEPL_DISPLAY_DRIVER_COMMON_H
#define OEPL_DISPLAY_DRIVER_COMMON_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#define EMIT_INSTRUCTION_BUFFER(...)                          \
  do {                                                        \
    const uint8_t local_buffer[] = __VA_ARGS__;               \
    oepl_display_driver_common_instruction(                   \
      local_buffer, sizeof(local_buffer), false);             \
  } while(0)

#define EMIT_INSTRUCTION_STATIC_DATA(OPCODE, ...)             \
  do {                                                        \
    const uint8_t local_buffer[] = __VA_ARGS__;               \
    oepl_display_driver_common_instruction_with_data(         \
      OPCODE, local_buffer, sizeof(local_buffer), false);     \
  } while(0)

#define EMIT_INSTRUCTION_STATIC_DATA_BOTH(OPCODE, ...)        \
  do {                                                        \
    const uint8_t local_buffer[] = __VA_ARGS__;               \
    oepl_display_driver_common_instruction_with_data_multi(   \
      OPCODE, local_buffer, sizeof(local_buffer), false,      \
      CS_LEADER | CS_FOLLOWER);                               \
  } while(0)

#define EMIT_INSTRUCTION_STATIC_DATA_FOLLOWER(OPCODE, ...)    \
  do {                                                        \
    const uint8_t local_buffer[] = __VA_ARGS__;               \
    oepl_display_driver_common_instruction_with_data_multi(   \
      OPCODE, local_buffer, sizeof(local_buffer), false,      \
      CS_FOLLOWER);                                           \
  } while(0)

#define EMIT_INSTRUCTION_VAR_DATA(OPCODE, ...)                \
  do {                                                        \
    uint8_t local_buffer[] = __VA_ARGS__;                     \
    oepl_display_driver_common_instruction_with_data(         \
      OPCODE, local_buffer, sizeof(local_buffer), false);     \
  } while(0)

#define EMIT_INSTRUCTION_NO_DATA(OPCODE)                      \
  do {                                                        \
    oepl_display_driver_common_instruction(OPCODE, false);    \
  } while(0);

#define CS_LEADER    0x01
#define CS_FOLLOWER  0x02
typedef enum {
  BUSY_TIMEOUT,
  BUSY_DEASSERTED,
  SCAN_COMPLETE
} oepl_display_driver_common_event_t;
typedef void (*oepl_display_driver_common_callback_t)(oepl_display_driver_common_event_t event);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
void oepl_display_driver_common_init();
void oepl_display_driver_common_activate();
void oepl_display_driver_common_pulse_reset(uint32_t ms_before_assert, uint32_t ms_to_assert, uint32_t ms_after_assert);
void oepl_display_driver_common_deactivate();

void oepl_display_driver_common_instruction(uint8_t opcode, bool keep_cs_low);
void oepl_display_driver_common_instruction_multi(uint8_t opcode, bool keep_cs_low, uint8_t cs_mask);
void oepl_display_driver_common_instruction_with_data(uint8_t opcode, const uint8_t* data_buffer, size_t data_len, bool keep_cs_low);
void oepl_display_driver_common_instruction_with_data_multi(uint8_t opcode, const uint8_t* data_buffer, size_t data_len, bool keep_cs_low, uint8_t cs_mask);
void oepl_display_driver_common_data(const uint8_t* data_buffer, size_t data_len, bool keep_cs_low);
void oepl_display_driver_common_data_multi(const uint8_t* data_buffer, size_t data_len, bool keep_cs_low, uint8_t cs_mask);
void oepl_display_driver_common_dataread(uint8_t* data_buffer, size_t data_len, bool keep_cs_low);
void oepl_display_driver_common_transaction_done(void);
void oepl_display_driver_common_transaction_start_multi(uint8_t cs_mask);
void oepl_display_driver_common_transaction_done_multi(uint8_t cs_mask);

void oepl_display_scan_frame(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY);
void oepl_display_scan_frame_async(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, oepl_display_driver_common_callback_t cb_done);
void oepl_display_scan_frame_multi(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, uint8_t cs_mask);
void oepl_display_scan_frame_async_multi(uint8_t* xbuf, size_t bufsize, size_t xstart, size_t xbytes, size_t ystart, size_t ylines, int color, bool mirrorX, bool mirrorY, uint8_t cs_mask, oepl_display_driver_common_callback_t cb_done);

void oepl_display_driver_wait(size_t timeout_ms);
void oepl_display_driver_wait_busy(size_t timeout_ms, bool expected_pin_state);
void oepl_display_driver_wait_busy_async(oepl_display_driver_common_callback_t cb_idle, size_t timeout_ms, bool expected_pin_state);

#endif