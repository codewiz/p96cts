// SPDX-License-Identifier: 0BSD
//
// Pixel array testcases.
//
// Hand the driver a buffer and a format and let it convert, the way a picture
// viewer does. The three scenes are the three ways to name the source pixels:
// pens through graphics.library, pens through the RTG library, and RGB.

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "countof.h"
#include "gfx.h"
#include "rtg.h"

// Mostly not multiples of 16, the granule the PixelArray8 calls round rows up
// to, so a driver working in whole granules gets these wrong.
struct Rect {
    SHORT x, y, w, h;
};

static const struct Rect RECTS[] = {
    {  3,  4, 16, 12}, // one granule, unaligned destination
    { 25,  4, 17, 12}, // one past it
    { 48,  4, 31, 12}, // one short of two
    { 85,  4,  1, 12}, // a single column
    {  3, 22, 33, 14},
    { 44, 22, 15, 14},
    { 66, 22, 20,  1}, // a single row
    {  3, 40, 83, 20},
};
#define NRECTS ((int)countof(RECTS))

// Big enough for the furthest origin plus the widest rectangle; src_fits()
// rechecks, since undersizing it reads off the end of every row.
#define SRC_W 192
#define SRC_H 128

// Diagonal bands so a misplaced blit steps sideways against its neighbours,
// over a one-pixel checker so even a one-pixel shift changes pen. Pens 0 to 5
// are skipped, since the harness overrides those with named colors.
static UBYTE src_pen(SHORT x, SHORT y) {
    int band = ((x + y) >> 3) % 25; // 25 bands, each 8 pixels across
    int checker = (x ^ y) & 1;

    return (UBYTE)(6 + band * 10 + checker * 5);
}

// Every blit reads from a different origin, none of them (0, 0), so a driver
// that ignores (sx, sy) draws the right picture from the wrong place.
static void src_origin(int r, SHORT *sx, SHORT *sy) {
    *sx = (SHORT)(3 + r * 11);
    *sy = (SHORT)(2 + r * 7);
}

// A source rectangle running off the buffer is the test's bug, not a driver's.
static bool src_fits(int r, const struct Rect *c) {
    SHORT sx, sy;

    src_origin(r, &sx, &sy);
    return sx + c->w <= SRC_W && sy + c->h <= SRC_H;
}

// Not the shared backdrop: that is painted with the very calls this group
// tests, so a broken write would corrupt the surface the blits are read
// against. Coarse cells suffice -- catching a displacement is the source
// image's job, this only has to differ from it.
static void checkerboard(struct RastPort *rp, SHORT w, SHORT h) {
    // src_pen() only yields values congruent to 1 mod 5, so anything else is
    // free. These are (72,72,85) and (72,108,85): one step apart on the green
    // gun alone, quiet enough to read the blits over.
    const int pen_a = 82, pen_b = 90;
    const SHORT cell = 8;

    for (SHORT y = 0; y < h; y += cell) {
        SHORT y2 = y + cell - 1 < h ? y + cell - 1 : h - 1;

        for (SHORT x = 0; x < w; x += cell) {
            SHORT x2 = x + cell - 1 < w ? x + cell - 1 : w - 1;
            bool odd = ((x / cell) + (y / cell)) & 1;

            gfx_fill(rp, x, y, x2, y2, gfx_pen(odd ? pen_a : pen_b));
        }
    }
}

// --- pens8 ------------------------------------------------------------------

// graphics.library's own chunky write: pen-based, so palette-only. Array rows
// are padded to a multiple of 16 pixels and the whole padded row is converted;
// the destination is not, which is what the odd widths check.
static void t_pens8(struct RastPort *rp, SHORT w, SHORT h) {
    checkerboard(rp, w, h);

    for (int r = 0; r < NRECTS; r++) {
        const struct Rect *c = &RECTS[r];
        SHORT stride = (SHORT)(((c->w + 15) >> 4) << 4);
        SHORT sx, sy;
        UBYTE *px;

        if (c->x + c->w > w || c->y + c->h > h)
            continue;

        // Rebuilt per blit: WritePixelArray8 converts in place and is
        // documented to destroy the array.
        px = (UBYTE *)AllocVec((ULONG)stride * c->h, MEMF_ANY);
        if (!px)
            return;

        src_origin(r, &sx, &sy);
        for (SHORT yy = 0; yy < c->h; yy++)
            for (SHORT xx = 0; xx < stride; xx++)
                px[yy * stride + xx] = src_pen(sx + xx, sy + yy);

        gfx_write_pens(rp, c->x, c->y, c->w, c->h, px, 8);
        FreeVec(px);
    }
}

// --- lut8 -------------------------------------------------------------------

// The same pens through the RTG library: p96WritePixelArray with an RGBFB_CLUT
// RenderInfo, which CGX spells WritePixelArray with RECTFMT_LUT8. Takes a
// source rectangle, so one buffer serves every blit. On truecolor screens the
// pens resolve through the palette, a conversion nothing else here reaches.
static void t_lut8(struct RastPort *rp, SHORT w, SHORT h) {
    UBYTE *px = (UBYTE *)AllocVec((ULONG)SRC_W * SRC_H, MEMF_ANY);

    checkerboard(rp, w, h);
    if (!px)
        return;

    for (SHORT yy = 0; yy < SRC_H; yy++)
        for (SHORT xx = 0; xx < SRC_W; xx++)
            px[yy * SRC_W + xx] = src_pen(xx, yy);

    for (int r = 0; r < NRECTS; r++) {
        const struct Rect *c = &RECTS[r];
        SHORT sx, sy;

        if (c->x + c->w > w || c->y + c->h > h || !src_fits(r, c))
            continue;
        src_origin(r, &sx, &sy);
        rtg_write_pens(rp, c->x, c->y, c->w, c->h, px, sx, sy, SRC_W);
    }
    FreeVec(px);
}

// --- rgb --------------------------------------------------------------------

// Three bytes per pixel, repacked into the screen's own byte order: an
// a8r8g8b8 board and a b8g8r8a8 one must agree on screen. Truecolor only --
// the CGX autodoc allows only RECTFMT_LUT8 at depths <= 8, and P96 hangs if
// asked anyway.
static void t_rgb(struct RastPort *rp, SHORT w, SHORT h) {
    UBYTE *px = (UBYTE *)AllocVec((ULONG)SRC_W * SRC_H * 3, MEMF_ANY);

    checkerboard(rp, w, h);
    if (!px)
        return;

    for (SHORT yy = 0; yy < SRC_H; yy++)
        for (SHORT xx = 0; xx < SRC_W; xx++) {
            ULONG rgb = pen_rgb(src_pen(xx, yy));
            ULONG p = ((ULONG)yy * SRC_W + xx) * 3;

            px[p] = (UBYTE)(rgb >> 16);
            px[p + 1] = (UBYTE)(rgb >> 8);
            px[p + 2] = (UBYTE)rgb;
        }

    for (int r = 0; r < NRECTS; r++) {
        const struct Rect *c = &RECTS[r];
        SHORT sx, sy;

        if (c->x + c->w > w || c->y + c->h > h || !src_fits(r, c))
            continue;
        src_origin(r, &sx, &sy);
        rtg_write_rgb(rp, c->x, c->y, c->w, c->h, px, sx, sy, SRC_W * 3);
    }
    FreeVec(px);
}

static const struct P96Test TESTS[] = {
    {.name = "pens8", .fn = t_pens8, .palette_only = true},
    {.name = "lut8", .fn = t_lut8},
    {.name = "rgb", .fn = t_rgb, .truecolor_only = true},
};

const struct P96TestGroup PixelArrayGroup = {
    "PixelArray", TESTS, (int)countof(TESTS)
};
