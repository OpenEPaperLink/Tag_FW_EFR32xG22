#ifndef OEPL_NFC_DRIVER_TNB132M_H
#define OEPL_NFC_DRIVER_TNB132M_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_nfc_driver_common.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
extern const oepl_nfc_driver_desc_t oepl_nfc_driver_tnb132m;

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------
bool oepl_nfc_init_tnb132m(const oepl_efr32xg22_nfcconfig_t* cfg);
bool oepl_nfc_write_tnb132m(const oepl_efr32xg22_nfcconfig_t* cfg, oepl_nfc_buffer_type_t content_type, const uint8_t* raw_buffer, size_t length);

#endif