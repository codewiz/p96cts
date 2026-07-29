// SPDX-License-Identifier: 0BSD
//
// The shared source image and the two forms scenes blit it in (glyph.c).

#ifndef P96CTS_GLYPH_INCLUDED
#define P96CTS_GLYPH_INCLUDED

#include <exec/types.h>
#include <graphics/gfx.h>
#include <stdbool.h>

#define P96CTS_GLYPH_W 48
#define P96CTS_GLYPH_H 16
#define P96CTS_GLYPH_MOD (P96CTS_GLYPH_W / 8) // bytes per one-bit-deep row

extern const char *const glyph[P96CTS_GLYPH_H];

UBYTE glyph_pen(SHORT x, SHORT y);

PLANEPTR glyph_template(void);
void glyph_free_template(PLANEPTR tpl);

// A planar BitMap of the glyph's size, filled from pen_at.
struct GlyphPlanar {
    struct BitMap bm;
    PLANEPTR ones; // the shared sentinel plane, freed once
};

bool glyph_planar(struct GlyphPlanar *p, int depth,
                         UBYTE (*pen_at)(SHORT x, SHORT y));
void glyph_free_planar(struct GlyphPlanar *p);

#endif
