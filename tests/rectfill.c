// SPDX-License-Identifier: 0BSD
//
// RectFill() testcases.
//
// P96 specifies RectFill as the cross product of the two JAM modes with
// INVERSVID and COMPLEMENT, all of it further constrained by rp->Mask, which
// keeps the unselected destination bits. A driver that maps the rectangle
// onto a hardware fill typically gets JAM1 right and everything else wrong,
// so the scenes sweep the whole grid rather than sampling it.

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"

// Tiles: 8 drawmode columns across, one row per write mask. Sized from the
// scene rather than fixed, so the grid still fits when SCENE is smaller than
// the default: the RastPort has no Layer, so anything drawn past the bitmap
// corrupts memory instead of being clipped.
#define COLS 8

// Pens with their bits spread across the byte, so a mask that keeps only
// some bit positions produces a value distinct from both operands. Pen 1 and
// pen 2 (P96Tests' choice) differ in one bit each, which lets a driver that
// mishandles the mask land on the right answer by accident.
#define FG 0x5A
#define BG 0xA5
#define SCENE_BG 0x35
#define BAND_BG 0xC6

// The truecolor equivalents, all channel-asymmetric: a driver that swaps
// red and blue, or renders BGRA data as RGBA, produces a different pixel
// for every one of these.
#define FG_RGB 0xE07010UL       // orange
#define BG_RGB 0x8020C0UL       // purple
#define SCENE_BG_RGB 0x203040UL // dark slate
#define BAND_BG_RGB 0x10C0E0UL  // cyan

static const UBYTE MASKS[] = {0xFF, 0x0F, 0x55, 0x81};
#define NMASKS ((SHORT)(sizeof MASKS / sizeof MASKS[0]))

// The eight mode combinations, in the order P96Tests renders them.
static const UBYTE MODES[COLS] = {
    JAM1,
    JAM1 | INVERSVID,
    JAM1 | COMPLEMENT,
    JAM1 | INVERSVID | COMPLEMENT,
    JAM2,
    JAM2 | INVERSVID,
    JAM2 | COMPLEMENT,
    JAM2 | INVERSVID | COMPLEMENT,
};

// One row of the grid: eight tiles at one write mask, drawn over a horizontal
// band of a second tone. The band is what makes COMPLEMENT and the JAM2
// background pen visible -- against a single flat backdrop several of these
// modes collapse onto the same output.
static void mode_row(struct RastPort *rp, SHORT x, SHORT y, SHORT tile,
                     SHORT mask) {
    SHORT inset = tile / 4;

    rp->Mask = 0xFF;
    SetDrMd(rp, JAM1);
    SetAPen(rp, BAND_BG);
    RectFill(rp, x, y + inset, x + COLS * tile - 1, y + tile - inset - 1);

    SetAPen(rp, FG);
    SetBPen(rp, BG);
    rp->Mask = (UBYTE)mask;

    for (SHORT c = 0; c < COLS; c++) {
        SetDrMd(rp, MODES[c]);
        RectFill(rp, x + c * tile, y, x + (c + 1) * tile - 1, y + tile - 1);
    }

    rp->Mask = 0xFF;
    SetDrMd(rp, JAM1);
    SetBPen(rp, 0);
}

static void t_drawmodes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT tile = w / COLS;
    SHORT pitch = h / NMASKS;
    SHORT x = (w - COLS * tile) / 2;

    if (tile > pitch)
        tile = pitch;

    p96cts_clear(rp, w, h, SCENE_BG);
    for (SHORT r = 0; r < NMASKS; r++)
        mode_row(rp, x, r * pitch + (pitch - tile) / 2, tile, MASKS[r]);
}

// Degenerate geometry.
//
// The graphics.library's RectFill() contract requires (xmax >= xmin) and
// (ymax >= ymin). A single pixel and a single row or column are legal fills,
// of width and height 1.
//
// P96's driver hook is FillRect(x, y, width, height), so something between
// the two has to convert, and a conversion that computes width as
// (xmax - xmin) rather than (xmax - xmin + 1) drops those fills entirely.
//
// Corners the wrong way round are deliberately not tested: the autodoc makes
// (xmax >= xmin) a precondition the caller must meet, so a P96 driver is free
// to do anything with a reversed rectangle and there is nothing to conform to.
static void t_edges(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT n = (w < h ? w : h) / 8;
    ULONG fg = p96cts_color(FG, FG_RGB);
    ULONG bg = p96cts_color(BG, BG_RGB);
    ULONG band = p96cts_color(BAND_BG, BAND_BG_RGB);

    p96cts_clear(rp, w, h, p96cts_color(SCENE_BG, SCENE_BG_RGB));

    // A diagonal of single pixels, then single-pixel rows and columns of
    // growing length, so an off-by-one in either axis shows up as a short
    // run rather than as nothing at all.
    for (SHORT i = 0; i < n; i++)
        p96cts_fill(rp, 2 + i * 4, 2 + i * 4, 2 + i * 4, 2 + i * 4, fg);

    for (SHORT i = 0; i < n; i++)
        p96cts_fill(rp, w / 2, 2 + i * 4, w / 2 + i, 2 + i * 4, band);

    for (SHORT i = 0; i < n; i++)
        p96cts_fill(rp, 2 + i * 4, h / 2, 2 + i * 4, h / 2 + i, bg);

    // The scene's own edges, where a fill that is off by one writes outside
    // the bitmap entirely.
    p96cts_fill(rp, 0, 0, w - 1, 0, fg);
    p96cts_fill(rp, 0, h - 1, w - 1, h - 1, fg);
    p96cts_fill(rp, 0, 0, 0, h - 1, fg);
    p96cts_fill(rp, w - 1, 0, w - 1, h - 1, fg);
}

// COMPLEMENT inverts the destination and needs no color at all, which makes
// it the one drawmode that works identically on palette and truecolor
// screens -- on truecolor it is the driver's InvertRect. The double-inverted
// rectangle is the heart of the scene: it must come back bit-exact, which no
// "fill with something plausible" implementation survives.
static void t_invert(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, p96cts_color(SCENE_BG, SCENE_BG_RGB));

    // Three bands, so the inversions below each cross a color boundary.
    p96cts_fill(rp, 0, 0, w / 3 - 1, h - 1, p96cts_color(FG, FG_RGB));
    p96cts_fill(rp, w / 3, 0, 2 * w / 3 - 1, h - 1,
                p96cts_color(BAND_BG, BAND_BG_RGB));
    p96cts_fill(rp, 2 * w / 3, 0, w - 1, h - 1, p96cts_color(BG, BG_RGB));

    SetDrMd(rp, COMPLEMENT);
    // One inversion across all three bands; one double inversion, which
    // must restore the bands exactly; one single pixel.
    RectFill(rp, w / 8, h / 4, w - w / 8 - 1, h / 2 - 1);
    RectFill(rp, w / 8, h - h / 3, w - w / 8 - 1, h - h / 6);
    RectFill(rp, w / 8, h - h / 3, w - w / 8 - 1, h - h / 6);
    RectFill(rp, w / 2, h * 3 / 4, w / 2, h * 3 / 4);
    SetDrMd(rp, JAM1);
}

static const struct P96Test TESTS[] = {
    // The drawmode grid stays palette only, and permanently: it sweeps every
    // mode across rp->Mask, which selects bitplanes and has no truecolor
    // counterpart. The pens themselves are fine -- BPen resolves through the
    // screen's palette at any depth.
    {"drawmodes", t_drawmodes, true},
    {"edges", t_edges, false},
    {"invert", t_invert, false},
};

const struct P96TestGroup RectFillGroup = {
    "RectFill", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
