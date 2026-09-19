#ifndef OEPL_NFC_DRIVER_COMMON_H
#define OEPL_NFC_DRIVER_COMMON_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "oepl_efr32_hwtypes.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
typedef enum {
  RAW_NDEF_BUFFER,
  URI,
  TEXT,
  MIME,
  WELL_KNOWN_RAW
} oepl_nfc_buffer_type_t;

typedef bool (*oepl_nfc_driver_init_t)(const oepl_efr32xg22_nfcconfig_t* cfg);
typedef bool (*oepl_nfc_driver_write_t)(const oepl_efr32xg22_nfcconfig_t* cfg, oepl_nfc_buffer_type_t content_type, const uint8_t* raw_buffer, size_t length);

typedef struct {
  oepl_nfc_driver_init_t init;
  oepl_nfc_driver_write_t write;
} oepl_nfc_driver_desc_t;

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
bool oepl_nfc_init(void);
bool oepl_nfc_write_url(const uint8_t* url_buffer, size_t length);
bool oepl_nfc_write_raw(const uint8_t* raw_buffer, size_t length);
bool oepl_nfc_write(oepl_nfc_buffer_type_t content_type, const uint8_t* raw_buffer, size_t length);
bool oepl_nfc_is_writing(void);

#endif