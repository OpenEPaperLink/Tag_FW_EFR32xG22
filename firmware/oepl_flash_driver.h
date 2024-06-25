#ifndef OEPL_FLASH_DRIVER_H
#define OEPL_FLASH_DRIVER_H

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdint.h>

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//                          Public Function Declarations
// -----------------------------------------------------------------------------

// Function called by the Drawing module when it needs to read from a flash-
// stored image.
// Returns the amount of bytes read (but callers really just expect this to work)
uint32_t HAL_flashRead(uint32_t address, uint8_t *buffer, uint32_t num);

#endif