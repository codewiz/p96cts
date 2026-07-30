// SPDX-License-Identifier: 0BSD
//
// ScrollRaster() testcases.
//
// ScrollRaster is the only call the suite covers that pairs an overlapping blit
// with a fill: the scroll is a copy of the region that stays on screen, and the
// strip it vacates is a RectFill. Both reach the driver, but unlike every other
// group here the fill's pen and draw mode are chosen by graphics.library rather
// than by the scene. That is a blind spot worth closing on its own -- a change
// to RectFill's draw mode handling once stopped scrolling clearing anything at
// all, while every direct RectFill and blit scene here still passed.
//
// dx and dy are the distance the contents move towards (0,0), so a positive dy
// scrolls the picture up and vacates a strip along the bottom edge.

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"
#include "backdrop.h"

// Pens with their bits spread across the byte, so a wrong one is not a near
// miss of a right one.
#define FG 0x5A
#define BG 0xA5
#define STRIPE_A 0x1C
#define STRIPE_B 0x63
#define VACATED 0xC6
#define SCENE_BG 0x35

// --- directions -------------------------------------------------------------

// Scroll a quadrant, inset by a margin so undisturbed pixels are left on all
// four sides: that is what a displacement is read against, and it also catches
// a scroll that writes outside the rectangle it was given.
static void scroll_quadrant(struct RastPort *rp, SHORT qx, SHORT qy, SHORT qw,
                            SHORT qh, SHORT dx, SHORT dy) {
    SHORT m = 6;

    ScrollRaster(rp, dx, dy, qx + m, qy + m, qx + qw - 1 - m, qy + qh - 1 - m);
}

// All four directions at once, a quadrant each, over the backdrop. A scroll
// that walks its copy the wrong way smears one row or column through the
// picture rather than moving it, which is the same failure ClipBlit-overlap
// looks for but reached through graphics.library's own blit call.
//
// The distances are odd and unequal so no two quadrants can agree by accident.
// Odd matters for its own reason too: the backdrop is dithered on a 4x4 cell,
// so a multiple of 4 would leave the pattern looking untouched and hide a smear
// inside what reads as flat color.
static void t_directions(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT qw = w / 2, qh = h / 2;

    backdrop(rp, w, h);

    // JAM2 with a background pen, so the vacated strip lands on one definite
    // color however graphics.library asks for it. Which pen each draw mode
    // produces is t_drawmodes' subject, not this one's.
    SetABPenDrMd(rp, gfx_pen(FG), gfx_pen(BG), JAM2);

    scroll_quadrant(rp, 0, 0, qw, qh, 0, 17);   // up
    scroll_quadrant(rp, qw, 0, qw, qh, 0, -21); // down
    scroll_quadrant(rp, 0, qh, qw, qh, 7, 0);   // left
    scroll_quadrant(rp, qw, qh, qw, qh, -9, 0); // right
}

// --- drawmodes --------------------------------------------------------------

#define COLS 8

// The eight mode combinations, in the order the RectFill and BltPattern grids
// render them.
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

// One tile: horizontal stripes above the strip the scroll will vacate, and the
// strip itself in a third tone.
//
// The stripe period does not divide the scroll distance, so the scroll has to
// land the stripes somewhere a phase error would not. Priming the vacated strip
// matters because a scroll leaves that strip holding its own old contents
// before the fill runs -- so under COMPLEMENT the result is this tone inverted,
// and under a mode that fills nothing at all it is this tone untouched. Both
// are then distinguishable from every pen the tile draws with.
static void mode_tile(struct RastPort *rp, SHORT x, SHORT y, SHORT tw, SHORT th,
                      SHORT band, UBYTE mode) {
    SHORT top = th - band;

    for (SHORT r = 0; r < top; r += 5) {
        SHORT last = (r + 4 < top) ? r + 4 : top - 1;

        gfx_fill(rp, x, y + r, x + tw - 1, y + last,
                 gfx_pen(((r / 5) & 1) ? STRIPE_A : STRIPE_B));
    }
    gfx_fill(rp, x, y + top, x + tw - 1, y + th - 1, gfx_pen(VACATED));

    // After the fills, because gfx_fill() sets the pen and the draw mode.
    SetABPenDrMd(rp, gfx_pen(FG), gfx_pen(BG), mode);
    ScrollRaster(rp, 0, band, x, y, x + tw - 1, y + th - 1);
}

// The eight draw modes across, one tile each, every tile scrolled up by the
// same distance. The tiles differ only in the mode, so the whole scene is a
// readout of how graphics.library asks for the vacated strip to be filled --
// the one thing about ScrollRaster a caller cannot see any other way.
static void t_drawmodes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT tw = w / COLS;
    SHORT th = h - 16;
    SHORT band = 13;

    gfx_clear(rp, w, h, gfx_pen(SCENE_BG));
    if (tw < 8 || th < band * 2)
        return;

    for (SHORT c = 0; c < COLS; c++)
        mode_tile(rp, c * tw, 8, tw, th, band, MODES[c]);
}

// --- amounts ----------------------------------------------------------------

// Zero, one either way, and a distance that crosses a 16-pixel word. OVER_EXTENT
// stands for one more than the extent being scrolled, resolved per axis below
// because the tiles are not square: it is the smallest distance that moves
// everything off. Together with the 4096 that follows it, it pins down what
// happens once there is nothing left to copy -- whether that is a distance the
// call declines outright or one it treats as vacating the whole rectangle, and
// whether the two answers agree at 4096.
#define OVER_EXTENT 32767
static const short AMOUNTS[] = {0, 1, -1, 15, -17, OVER_EXTENT, 4096};

static void t_amounts(struct RastPort *rp, SHORT w, SHORT h) {
    int n = (int)(sizeof AMOUNTS / sizeof AMOUNTS[0]);
    SHORT tw = w / n, th = h / 2;
    SHORT rectw = tw - 4, recth = h - 3 - (th + 2) + 1;

    backdrop(rp, w, h);
    SetABPenDrMd(rp, gfx_pen(FG), gfx_pen(BG), JAM2);

    if (tw < 8 || rectw < 4 || recth < 4)
        return;

    // Horizontally on the top row and vertically on the bottom one, so a
    // distance handled correctly in one axis and not the other cannot pass.
    for (int i = 0; i < n; i++) {
        SHORT x = i * tw;
        SHORT dx = AMOUNTS[i] == OVER_EXTENT ? rectw + 1 : AMOUNTS[i];
        SHORT dy = AMOUNTS[i] == OVER_EXTENT ? recth + 1 : AMOUNTS[i];

        ScrollRaster(rp, dx, 0, x + 2, 2, x + tw - 3, th - 3);
        ScrollRaster(rp, 0, dy, x + 2, th + 2, x + tw - 3, h - 3);
    }
}

static const struct P96Test TESTS[] = {
    {"directions", t_directions, false},
    {"drawmodes", t_drawmodes, false},
    {"amounts", t_amounts, false},
};

const struct P96TestGroup ScrollRasterGroup = {
    "ScrollRaster", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
