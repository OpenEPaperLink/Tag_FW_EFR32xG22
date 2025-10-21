/******************************************************************************
 * This file contains the C API part of the CPP-written OEPL 'Drawing' module.
 *****************************************************************************/
#ifndef OEPL_DRAWING_CAPI_H
#define OEPL_DRAWING_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define COLOR_RED 1
#define COLOR_BLACK 0
#define COLOR_DUAL 2
#define COLOR_YELLOW 2

#define IMAGE_OR 1
#define IMAGE_REPLACE 0

#define DRAW_INVERTED 1
#define DRAW_NORMAL 0

typedef enum rotation {
    ROTATE_0,
    ROTATE_90,
    ROTATE_180,
    ROTATE_270
} rotation_t;

typedef struct __attribute__((packed)) {
    uint16_t bitmapOffset;  ///< Pointer into GFXfont->bitmap
    uint8_t width;          ///< Bitmap dimensions in pixels
    uint8_t height;         ///< Bitmap dimensions in pixels
    uint8_t xAdvance;       ///< Distance to advance cursor (x axis)
    int8_t xOffset;         ///< X dist from cursor pos to UL corner
    int8_t yOffset;         ///< Y dist from cursor pos to UL corner
} GFXglyph;

/// Data stored for FONT AS A WHOLE
typedef struct {
    uint8_t *bitmap;   ///< Glyph bitmaps, concatenated
    GFXglyph *glyph;   ///< Glyph array
    uint16_t first;    ///< ASCII extents (first char)
    uint16_t last;     ///< ASCII extents (last char)
    uint8_t yAdvance;  ///< Newline distance (y axis)
} GFXfont;

void C_setDisplayParameters(bool c_drawDirectionRight, uint32_t c_effectiveXRes, uint32_t c_effectiveYRes);

// C function to printf on the current canvas
void C_epdSetFont(const GFXfont* font);
void C_epdPrintf(uint16_t x, uint16_t y, uint8_t color, rotation_t ro, const char *c, ...);

// C functions to add an image to the current canvas
void C_drawFlashFullscreenImageWithType(uint32_t addr, uint8_t type, uint32_t filesize);
void C_addBufferedImage(uint16_t x, uint16_t y, uint8_t color, rotation_t ro, const uint8_t *image, bool mask);
void C_addFlashImage(uint16_t x, uint16_t y, uint8_t color, rotation_t ro, const uint8_t *image);
void C_addQR(uint16_t x, uint16_t y, uint8_t version, uint8_t scale, const char *c, ...);
void C_drawRoundedRectangle(uint16_t xpos, uint16_t ypos, uint16_t width, uint16_t height, uint8_t color);
void C_drawMask(uint16_t xpos, uint16_t ypos, uint16_t width, uint16_t height, uint8_t color);

// C function for the display driver to get a drawline
void C_renderDrawLine(uint8_t *line, uint16_t number, uint8_t c);
void C_flushDrawItems(void);

#ifdef __cplusplus
}
#endif

#endif // OEPL_DRAWING_CAPI_H