// SPDX-License-Identifier: 0BSD
//
// The shared backdrop scene.
//
// Testcases that need a non-trivial destination to draw over or copy around
// share this one, so a scene is recognizable across groups and only has to
// be justified once.

#include <exec/memory.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>

#include "backdrop.h"
#include "gfx.h"

// The backdrop is a landscape: an off-center sun over two irregular ridges,
// with rippled water below and a boat well off to one side.
//
// It is a picture rather than a ramp because a human has to read the diff.
// A misdirected blit in an abstract gradient is a subtle change of shade; in
// a landscape it visibly tears the horizon, which is the difference between
// spotting a driver bug and squinting at one.
//
// A picture alone would not do, though. Detecting a smear needs neighboring
// pixels to differ -- a flat sky would swallow a copy that replicated one row
// down the overlap -- so the gradients are ordered-dithered before being
// quantized to the palette, in the manner of a copper gradient.
//
// The dither cell is 4x4, which is what makes the scene work as a test: a
// shift of (dx, dy) leaves the pattern unchanged only when both dx and dy are
// multiples of 4, and the shifts used by the testcases (5 rows, 3 columns)
// are neither. Three quarters of the pixels in any smeared region therefore
// land on the wrong dither phase, even inside what looks like flat color.
//
// Written through WritePixelArray8; a RectFill per pixel would be tens of
// thousands of driver calls per scene.

// Integer hash of one value, for the terrain outline. Knuth's multiplicative
// constant; the shift picks bits that vary quickly.
static int hash8(int i) {
    unsigned int u = (unsigned int)i * 2654435761U;
    return (int)((u >> 13) & 0xFF);
}

// Ordered (Bayer) dither, the classic 4x4 recursive matrix. Preferred over
// error diffusion here because it is a pure function of the coordinates: the
// scene stays identical whatever order a driver happens to render in, and it
// costs one table lookup per pixel.
static const UBYTE BAYER[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// A ridge line: hashed control points every `step` pixels, linearly
// interpolated. Irregular and non-repeating, unlike a sine.
static int ridge(int x, int seed, int amp, int base, int step) {
    int i = x / step, f = x % step;
    int a = hash8(i + seed) * amp / 255;
    int b = hash8(i + 1 + seed) * amp / 255;
    return base - (a + (b - a) * f / step);
}

static UBYTE clamp8(int v) {
    return (UBYTE)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Quantize to the 3-3-2 palette cube (see palette.c). Pens 0-5 are overridden
// there with named colors, so a scene color landing on one would display as
// e.g. white in the middle of a dark red; push those up into the blue half of
// the cube instead.
static UBYTE pen_of(int r, int g, int b) {
    int pen = (clamp8(r) >> 5) | ((clamp8(g) >> 5) << 3) | ((clamp8(b) >> 6) << 6);
    return (UBYTE)(pen < 6 ? pen | 0x40 : pen);
}

void p96cts_backdrop(struct RastPort *rp, SHORT w, SHORT h) {
    int horizon = h * 3 / 5;
    int sun_x = w * 7 / 10, sun_y = h * 7 / 25, sun_r = h / 7;
    int boat_x = w / 4, boat_y = horizon + h / 5;
    // Big enough that the mast and sail cross the edges of a half-quadrant
    // rectangle, so a copy that takes part of the boat and leaves the rest
    // breaks it at a high-contrast edge. A boat sitting wholly inside the
    // rectangle would just translate intact and show nothing.
    int boat_w = w / 6, boat_h = h / 12;
    int bpp = p96cts_truecolor ? 3 : 1;

    UBYTE *px = AllocVec((ULONG)w * h * bpp, MEMF_ANY);
    if (!px)
        return;

    for (SHORT y = 0; y < h; y++) {
        for (SHORT x = 0; x < w; x++) {
            // Scaled to a bit under one palette step, so the dither shapes
            // the boundary between two adjacent shades rather than adding
            // visible speckle.
            int d = (BAYER[y & 3][x & 3] - 8) * 3;
            int dx = x - sun_x, dy = y - sun_y;
            int r, g, b;

            if (y < horizon) {
                // Sky: deep blue overhead warming to orange at the horizon.
                int t = y * 255 / horizon;
                r = 40 + t * 200 / 255;
                g = 60 + t * 120 / 255;
                b = 150 - t * 100 / 255;
                if (dx * dx + dy * dy < sun_r * sun_r) {
                    r = 255;
                    g = 230;
                    b = 120;
                }
            } else {
                // Water: the sky's horizon tone, darkening with depth, with
                // ripples that break up every row differently.
                int t = (y - horizon) * 255 / (h - horizon);
                // Ripples: horizontal bars whose phase shifts per row, so the
                // water varies along both axes without looking like noise.
                int rip = ((x + hash8(y) / 8) / 3 & 7) * 5;
                r = 120 - t * 80 / 255 + rip;
                g = 90 - t * 60 / 255 + rip;
                b = 110 + t * 40 / 255 + rip;
                // Sun's reflection, wobbling as it goes down the water.
                if (x > sun_x - sun_r / 2 + hash8(y * 3) / 16 - 8 &&
                    x < sun_x + sun_r / 2 + hash8(y * 3) / 16 - 8) {
                    r += 90;
                    g += 70;
                }
            }

            // Two ridges, the near one darker, both drawn over the sky.
            if (y > ridge(x, 11, h / 5, horizon, w / 10) && y < horizon) {
                r = 90;
                g = 70;
                b = 90;
            }
            if (y > ridge(x, 77, h / 8, horizon, w / 16) && y < horizon) {
                r = 45;
                g = 40;
                b = 55;
            }

            // A boat: hull, then mast and sail. Deliberately off to one side,
            // so the scene has no axis of symmetry a shifted copy could hide
            // behind.
            if (y >= boat_y && y < boat_y + boat_h &&
                x >= boat_x + (y - boat_y) && x < boat_x + boat_w - (y - boat_y)) {
                r = 30;
                g = 20;
                b = 25;
            }
            if (x >= boat_x + boat_w / 2 && x < boat_x + boat_w / 2 + 2 &&
                y < boat_y && y > boat_y - boat_h * 3)
                r = g = b = 20;
            if (y < boat_y && y > boat_y - boat_h * 3 &&
                x > boat_x + boat_w / 2 + 2 &&
                x < boat_x + boat_w / 2 + 2 + (y - (boat_y - boat_h * 3)) / 2) {
                r = 230;
                g = 225;
                b = 210;
            }

            if (p96cts_truecolor) {
                ULONG p = ((ULONG)y * w + x) * 3;
                px[p] = clamp8(r + d);
                px[p + 1] = clamp8(g + d);
                px[p + 2] = clamp8(b + d);
            } else {
                px[y * w + x] = pen_of(r + d, g + d, b + d);
            }
        }
    }

    if (p96cts_truecolor) {
        // The buffer is already R8G8B8; p96WritePixelArray converts to
        // whatever the screen's format is.
        struct RenderInfo ri;
        ri.Memory = px;
        ri.BytesPerRow = w * 3;
        ri.pad = 0;
        ri.RGBFormat = RGBFB_R8G8B8;
        p96WritePixelArray(&ri, 0, 0, rp, 0, 0, w, h);
    } else {
        // WritePixelArray8 needs a single-row scratch RastPort, like the
        // readback in gfx.c.
        struct RastPort temprp = *rp;
        temprp.Layer = NULL;
        temprp.BitMap = AllocBitMap(w, 1, 8, 0, rp->BitMap);
        if (temprp.BitMap) {
            WritePixelArray8(rp, 0, 0, w - 1, h - 1, px, &temprp);
            FreeBitMap(temprp.BitMap);
        }
    }
    FreeVec(px);
}
