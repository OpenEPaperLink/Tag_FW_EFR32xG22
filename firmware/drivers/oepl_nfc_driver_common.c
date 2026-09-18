// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include "oepl_nfc_driver_common.h"
#include "oepl_hw_abstraction.h"
#include "oepl_efr32_hwtypes.h"

// Include all driver headers
#include "oepl_nfc_driver_tnb132m.h"

// -----------------------------------------------------------------------------
//                              Configuration values
// -----------------------------------------------------------------------------
#ifndef NFC_COMMON_DEBUG_PRINT
#define NFC_COMMON_DEBUG_PRINT 1
#endif

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
#if NFC_COMMON_DEBUG_PRINT
#define DPRINTF(fmt_, ...) oepl_hw_debugprint(DBG_NFC, (fmt_), ##__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
static const oepl_efr32xg22_nfcconfig_t* cfg = NULL;
static const oepl_nfc_driver_desc_t* drv = NULL;

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
bool oepl_nfc_init(void)
{
  if(cfg != NULL) {
    // already initialized
    return true;
  }

  const oepl_efr32xg22_tagconfig_t* tagcfg = oepl_efr32xg22_get_config();
  if(tagcfg == NULL) {
    oepl_hw_crash(DBG_NFC, false, "No tag config found\n");
  }

  if(tagcfg->nfc == NULL) {
    return false;
  }

  // Detect driver type
  if(tagcfg->hwtype == SOLUM_AUTODETECT) {
    // For all we know, all SoluM devices have a TNB132M
    if(oepl_nfc_driver_tnb132m.init(tagcfg->nfc)) {
      drv = &oepl_nfc_driver_tnb132m;
      cfg = tagcfg->nfc;
      return true;
    }
  }

  return false;
}

bool oepl_nfc_write_url(const uint8_t* url_buffer, size_t length)
{
  return oepl_nfc_write(URI, url_buffer, length);
}

bool oepl_nfc_write_raw(const uint8_t* raw_buffer, size_t length)
{
  return oepl_nfc_write(RAW_NDEF_BUFFER, raw_buffer, length);
}

bool oepl_nfc_write(oepl_nfc_buffer_type_t content_type, const uint8_t* raw_buffer, size_t length)
{
  if(cfg == NULL) {
    DPRINTF("Trying to write NFC without NFC support\n");
    return false;
  }

  return drv->write(cfg, content_type, raw_buffer, length);
}