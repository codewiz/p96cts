// SPDX-License-Identifier: 0BSD
//
// Everything gfx.c offers: the color helpers scenes draw through, and the
// scene readback the harness drives. What needs an RTG library rather than
// graphics.library is in rtg.h, and the display database is in modes.h.

#ifndef P96CTS_GFX_H
#define P96CTS_GFX_H

#include <exec/types.h>
#include <stdbool.h>

#include "palette.h"

struct RastPort;

// --- colors -----------------------------------------------------------------

// Whether the current run is on a truecolor mode (depth > 8). Testcases that
// run on both kinds of screen branch on this to pick their color calls:
// pens on a palette screen, direct RGB on truecolor.
extern bool gfx_truecolor;

// The color for the current run: a pen number on a palette screen, direct
// 0x00RRGGBB on truecolor. Scenes pass both and let the run pick. Inline
// because scenes call it once per drawing primitive, sometimes in a loop, and
// it compiles to a load and a select.
static inline ULONG gfx_color(ULONG pen, ULONG rgb) {
    return gfx_truecolor ? rgb : pen;
}

// One pen, named the way the current run needs it: the number on a palette
// screen, the color that number stands for on truecolor. For scenes that draw
// alongside pen-indexed data a driver resolves through the same palette, where
// gfx_color() cannot help because there is only one color to name.
static inline ULONG gfx_pen(int pen) {
    return gfx_truecolor ? pen_rgb(pen) : (ULONG)pen;
}

void gfx_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color);
void gfx_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color);

// --- readback ---------------------------------------------------------------

UBYTE *gfx_read_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                     int depth);
void gfx_write_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                    UBYTE *px, int depth);

#endif
