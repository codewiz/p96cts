// SPDX-License-Identifier: 0BSD
//
// ClipBlit() testcases, copying within one RastPort.
//
// A copy whose source and destination overlap is the case drivers get wrong:
// the copy must behave as if the source were read in full before any of the
// destination is written, which on hardware that blits in place means walking
// the rows and pixels in the direction that keeps already-written data out of
// the way. A driver that always walks forwards smears the leading row down
// the overlap, which looks plausible enough to survive casual testing --
// every scrolling text display exercises exactly this path.

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"
#include "backdrop.h"

// Copy a rectangle within the RastPort's own bitmap. ClipBlit is the
// documented way to do this and is what P96 routes to the driver's blit;
// minterm 0xC0 is a plain source copy.
static void selfblit(struct RastPort *rp, SHORT sx, SHORT sy, SHORT dx,
                     SHORT dy, SHORT w, SHORT h) {
    ClipBlit(rp, sx, sy, rp, dx, dy, w, h, 0xC0);
}

// Copy a rectangle from the middle of one quadrant to a spot (dx, dy) away,
// overlapping its source everywhere but the shifted edge. The rectangle is
// half the quadrant in each axis and centered, so undisturbed backdrop is left
// on all four sides to read the displacement against.
//
// The bounds check is not paranoia: these RastPorts have no Layer, so a blit
// reaching past the bitmap corrupts memory instead of being clipped, and the
// quadrant shrinks with SCENE.
static void shift_quadrant(struct RastPort *rp, SHORT qx, SHORT qy, SHORT qw,
                           SHORT qh, SHORT dx, SHORT dy) {
    SHORT bw = qw / 2, bh = qh / 2;
    SHORT sx = qx + (qw - bw) / 2, sy = qy + (qh - bh) / 2;

    if (sx + dx < qx || sx + dx + bw > qx + qw ||
        sy + dy < qy || sy + dy + bh > qy + qh)
        return;
    selfblit(rp, sx, sy, sx + dx, sy + dy, bw, bh);
}

// All four directions at once, a quadrant each. Down and up are separate
// because a driver typically gets one right and smears the other; left and
// right are separate again because one that decides its walk direction from y
// alone passes the vertical pair and fails the horizontal one.
//
// The shifts are odd and unequal so that no two quadrants can agree by
// accident, and odd matters on its own: the backdrop is dithered on a 4x4
// cell, so a shift by a multiple of 4 would leave the pattern unchanged and
// hide a smear inside what looks like flat color.
static void t_overlap(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT qw = w / 2, qh = h / 2;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w, h);

    shift_quadrant(rp, 0, 0, qw, qh, 0, 17);   // down
    shift_quadrant(rp, qw, 0, qw, qh, 0, -21); // up
    shift_quadrant(rp, 0, qh, qw, qh, 7, 0);   // right
    shift_quadrant(rp, qw, qh, qw, qh, -9, 0); // left
}

// Copies that do not overlap at all, plus the degenerate sizes. These must
// come out identical whichever direction the driver walks, so a failure here
// is a plain addressing bug rather than a direction bug.
//
// Everything lands on backdrop rather than on a cleared background, so a copy
// that falls short or lands askew shows as a seam in a picture instead of a
// smudge on black -- and the degenerate copies are taken from rows crossing
// the ridges and the boat, where a one-pixel error changes the color.
static void t_disjoint(struct RastPort *rp, SHORT w, SHORT h) {
    // The backdrop's horizon, where sky meets water: the ridges sit just above
    // it and the ripples just below, so a row through it and columns crossing
    // it are the highest-contrast material the scene has.
    SHORT horizon = h * 3 / 5;

    p96cts_backdrop(rp, w, h);

    selfblit(rp, 0, 0, w / 2, h / 3, w / 2, h / 3);

    // Single-pixel, single-row and single-column copies: the same off-by-one
    // that RectFill can have, on the blit path.
    for (SHORT i = 0; i < 16; i++) {
        selfblit(rp, i * 4, horizon - 12, w / 2 + i * 4, horizon - 10, 1, 1);
        selfblit(rp, 0, horizon + 4 + i, w / 2, horizon + 4 + i, i + 1, 1);
        selfblit(rp, i * 4, horizon - 8, w / 2 + i * 4, horizon + 24, 1, i + 1);
    }
}

static const struct P96Test TESTS[] = {
    {"overlap", t_overlap, false},
    {"disjoint", t_disjoint, false},
};

const struct P96TestGroup ClipBlitGroup = {
    "ClipBlit", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
