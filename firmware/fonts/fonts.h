#include "../oepl_drawing_capi.h" // Definition of GFXfont
#include "../oepl_build_flags.h"

extern const GFXfont FreeSans9pt7b;
extern const GFXfont FreeSansBold18pt7b;
#if defined(OEPL_SMALL_FLASH)
// Only layouts for >= 4.3" panels use the 24 pt font. Alias it to the
// 18 pt font so the ~9 kB of glyph data can be garbage-collected.
#define FreeSansBold24pt7b FreeSansBold18pt7b
#else
extern const GFXfont FreeSansBold24pt7b;
#endif