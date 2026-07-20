/* SPDX-License-Identifier: 0BSD */

/* RectFill() testcases.
 *
 * P96 specifies RectFill as the cross product of the two JAM modes with
 * INVERSVID and COMPLEMENT, all of it further constrained by rp->Mask, which
 * keeps the unselected destination bits. A driver that maps the rectangle
 * onto a hardware fill typically gets JAM1 right and everything else wrong,
 * so the scenes sweep the whole grid rather than sampling it.
 */

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"

/* Tiles: 8 drawmode columns across, one row per write mask. Sized from the
 * scene rather than fixed, so the grid still fits when SCENE is smaller than
 * the default: the RastPort has no Layer, so anything drawn past the bitmap
 * corrupts memory instead of being clipped. */
#define COLS 8

/* Pens with their bits spread across the byte, so a mask that keeps only
 * some bit positions produces a value distinct from both operands. Pen 1 and
 * pen 2 (P96Tests' choice) differ in one bit each, which lets a driver that
 * mishandles the mask land on the right answer by accident. */
#define FG 0x5A
#define BG 0xA5
#define SCENE_BG 0x35
#define BAND_BG 0xC6

static const UBYTE MASKS[] = {0xFF, 0x0F, 0x55, 0x81};
#define NMASKS ((SHORT)(sizeof MASKS / sizeof MASKS[0]))

/* The eight mode combinations, in the order P96Tests renders them. */
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

/* One row of the grid: eight tiles at one write mask, drawn over a horizontal
 * band of a second tone. The band is what makes COMPLEMENT and the JAM2
 * background pen visible -- against a single flat backdrop several of these
 * modes collapse onto the same output. */
static void mode_row(struct RastPort *rp, SHORT x, SHORT y, SHORT tile,
                     SHORT mask) {
    SHORT c, inset = tile / 4;

    rp->Mask = 0xFF;
    SetDrMd(rp, JAM1);
    SetAPen(rp, BAND_BG);
    RectFill(rp, x, y + inset, x + COLS * tile - 1, y + tile - inset - 1);

    SetAPen(rp, FG);
    SetBPen(rp, BG);
    rp->Mask = (UBYTE)mask;

    for (c = 0; c < COLS; c++) {
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
    SHORT r;

    if (tile > pitch)
        tile = pitch;

    p96cts_clear(rp, w, h, SCENE_BG);
    for (r = 0; r < NMASKS; r++)
        mode_row(rp, x, r * pitch + (pitch - tile) / 2, tile, MASKS[r]);
}

/* Degenerate geometry. P96 fills the rectangle inclusive of both corners, so
 * a single pixel and a single row/column are legal fills, and a driver that
 * computes width as (x1 - x0) rather than (x1 - x0 + 1) drops them entirely.
 * Swapped corners are the other half: graphics.library normalises them, and a
 * driver that passes them through unsorted fills nothing or runs away. */
static void t_edges(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT n = (w < h ? w : h) / 8;
    SHORT i;

    p96cts_clear(rp, w, h, SCENE_BG);
    SetDrMd(rp, JAM1);

    /* A diagonal of single pixels, then single-pixel rows and columns of
     * growing length, so an off-by-one in either axis shows up as a short
     * run rather than as nothing at all. */
    SetAPen(rp, FG);
    for (i = 0; i < n; i++)
        RectFill(rp, 2 + i * 4, 2 + i * 4, 2 + i * 4, 2 + i * 4);

    SetAPen(rp, BAND_BG);
    for (i = 0; i < n; i++)
        RectFill(rp, w / 2, 2 + i * 4, w / 2 + i, 2 + i * 4);

    SetAPen(rp, BG);
    for (i = 0; i < n; i++)
        RectFill(rp, 2 + i * 4, h / 2, 2 + i * 4, h / 2 + i);

    /* Corners given the wrong way round: graphics.library normalises them,
     * and a driver that passes them through unsorted fills nothing or runs
     * away. Then the scene's own edges, where a fill that is off by one
     * writes outside the bitmap entirely. */
    SetAPen(rp, FG);
    RectFill(rp, w - 2, h - 2, w - n, h - n);
    RectFill(rp, 0, 0, w - 1, 0);
    RectFill(rp, 0, h - 1, w - 1, h - 1);
    RectFill(rp, 0, 0, 0, h - 1);
    RectFill(rp, w - 1, 0, w - 1, h - 1);
}

static const struct P96Test TESTS[] = {
    {"drawmodes", t_drawmodes},
    {"edges", t_edges},
};

const struct P96TestGroup FillRectGroup = {
    "fillrect", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
