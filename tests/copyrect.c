/* SPDX-License-Identifier: 0BSD */

/* Rectangle copy testcases (ClipBlit within one RastPort).
 *
 * A copy whose source and destination overlap is the case drivers get wrong:
 * the copy must behave as if the source were read in full before any of the
 * destination is written, which on hardware that blits in place means walking
 * the rows and pixels in the direction that keeps already-written data out of
 * the way. A driver that always walks forwards smears the leading row down
 * the overlap, which looks plausible enough to survive casual testing --
 * every scrolling text display exercises exactly this path.
 */

#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"
#include "backdrop.h"

/* Copy a rectangle within the RastPort's own bitmap. ClipBlit is the
 * documented way to do this and is what P96 routes to the driver's blit;
 * minterm 0xC0 is a plain source copy. */
static void selfblit(struct RastPort *rp, SHORT sx, SHORT sy, SHORT dx,
                     SHORT dy, SHORT w, SHORT h) {
    ClipBlit(rp, sx, sy, rp, dx, dy, w, h, 0xC0);
}

/* Shift the middle of the scene by a few rows, which overlaps the source by
 * all but those rows. Down and up separately: a driver typically gets one
 * direction right and smears the other. */
static void t_overlap_down(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT bw = w / 2, bh = h / 2;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w, h);
    selfblit(rp, w / 4, h / 8, w / 4, h / 8 + 5, bw, bh);
}

static void t_overlap_up(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT bw = w / 2, bh = h / 2;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w, h);
    selfblit(rp, w / 4, h / 8 + 5, w / 4, h / 8, bw, bh);
}

/* The same overlap along x, where the smear is a horizontal streak. A driver
 * that decides its walk direction from y alone passes the two scenes above
 * and fails these. */
static void t_overlap_left(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT bw = w / 2, bh = h / 2;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w, h);
    selfblit(rp, w / 4 + 3, h / 4, w / 4, h / 4, bw, bh);
}

static void t_overlap_right(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT bw = w / 2, bh = h / 2;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w, h);
    selfblit(rp, w / 4, h / 4, w / 4 + 3, h / 4, bw, bh);
}

/* Copies that do not overlap at all, plus the degenerate sizes. These must
 * come out identical whichever direction the driver walks, so a failure here
 * is a plain addressing bug rather than a direction bug. */
static void t_disjoint(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    p96cts_backdrop(rp, w / 2, h);

    selfblit(rp, 0, 0, w / 2, 0, w / 2, h / 3);

    /* Single-pixel, single-row and single-column copies: the same off-by-one
     * that RectFill can have, on the blit path. */
    for (SHORT i = 0; i < 16; i++) {
        selfblit(rp, i * 4, h / 2, w / 2 + i * 4, h / 2, 1, 1);
        selfblit(rp, 0, h / 2 + 4 + i, w / 2, h / 2 + 4 + i, i + 1, 1);
        selfblit(rp, i * 4, h - h / 4, w / 2 + i * 4, h - h / 4, 1, i + 1);
    }
}

static const struct P96Test TESTS[] = {
    {"overlap-down", t_overlap_down, 0},
    {"overlap-up", t_overlap_up, 0},
    {"overlap-left", t_overlap_left, 0},
    {"overlap-right", t_overlap_right, 0},
    {"disjoint", t_disjoint, 0},
};

const struct P96TestGroup CopyRectGroup = {
    "copyrect", TESTS, (int)(sizeof TESTS / sizeof TESTS[0]), 0 /* any depth */
};
