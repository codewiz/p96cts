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

extern const char *const p96cts_glyph[P96CTS_GLYPH_H];

UBYTE p96cts_glyph_pen(SHORT x, SHORT y);

PLANEPTR p96cts_glyph_template(void);
void p96cts_glyph_free_template(PLANEPTR tpl);

// A planar BitMap of the glyph's size, filled from pen_at.
struct P96CTSPlanar {
    struct BitMap bm;
    PLANEPTR ones; // the shared sentinel plane, freed once
};

bool p96cts_glyph_planar(struct P96CTSPlanar *p, int depth,
                         UBYTE (*pen_at)(SHORT x, SHORT y));
void p96cts_glyph_free_planar(struct P96CTSPlanar *p);

#endif
