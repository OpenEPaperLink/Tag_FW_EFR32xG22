#include "fonts.h"

// On small-flash builds fonts.h aliases FreeSansBold24pt7b to the 18 pt
// font, so compiling the 24 pt glyph data here would produce a duplicate
// definition of FreeSansBold18pt7b.
#if !defined(OEPL_SMALL_FLASH)
#define PROGMEM
#include "../common/fonts/FreeSansBold24pt7b.h"
#endif
