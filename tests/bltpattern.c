// SPDX-License-Identifier: 0BSD
//
// BltPattern() testcases.
//
// BltPattern fills a rectangle through rp->AreaPtrn -- a 16-wide, 2^n-tall
// two-colour pattern set with SetAfPt() -- under the current draw mode, and
// optionally cookie-cuts the result to a caller-supplied 2D mask. It is a
// distinct driver hook (BlitPattern) from the plain FillRect that RectFill
// drives, with its own pattern-phase, pen and mask handling, so a driver can
// get solid fills perfect and still render every patterned fill wrong.
//
// Two things here are unique to this primitive and reachable through nothing
// else in the suite: the pattern phase, which graphics.library anchors so that
// abutting fills tile seamlessly, and the mask, which cuts an arbitrary shape
// out of the fill. The golden is captured from P96's own software rasteriser,
// so whatever phase and mask geometry it produces is by definition what a
// conforming driver must reproduce.
//
// Note on the mask argument: a NULL mask (fill the whole rectangle) takes a
// separate "fast" path in P96's shared code that renders a repeated identical
// fill only when a pen or draw-mode setter has run since the last one -- an
// AreaPtrn or WaitBlit in between is not enough. The drawmodes grid changes the
// mode every tile, so it exercises the NULL path legitimately; the mask and
// phase scenes want many fills at one mode, so they pass an explicit all-ones
// mask, which always renders. Filling through an all-ones mask is defined to be
// the same as filling the whole rectangle.

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"

// A down-left diagonal, three pixels wide on an eight-pixel period. Sixteen
// wide is two periods and eight tall is exactly one, so the pattern tiles
// seamlessly in both axes: a phase error breaks the diagonal at a seam instead
// of hiding inside a repeat, and a driver that mirrors the pattern turns the
// "/" into a "\". SetAfPt takes 2^size rows, so PAT_H must stay a power of two.
#define PAT_W 16
#define PAT_H 8
#define PAT_SIZE 3

// Chip memory: on a native planar screen graphics.library feeds AreaPtrn to
// the blitter, which cannot see fast RAM -- a fast-memory pattern renders as
// garbage there while working on every RTG board.
static UWORD *build_pattern(void) {
    UWORD *pat = AllocVec(PAT_H * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);

    if (!pat)
        return NULL;

    for (int y = 0; y < PAT_H; y++)
        for (int x = 0; x < PAT_W; x++)
            if (((x + y) & 7) < 3)
                pat[y] |= (UWORD)(0x8000u >> x);

    return pat;
}

// Bytes per row of a mask wide enough for w pixels, rounded up to a word.
static SHORT mask_bytes(SHORT w) {
    return (SHORT)(((w + 15) >> 4) << 1);
}

// An all-ones mask: fills every pixel of the rectangle, the reliable stand-in
// for a NULL mask. Chip memory for the same blitter reason as the pattern.
static UBYTE *solid_mask(SHORT w, SHORT h) {
    SHORT bc = mask_bytes(w);
    UBYTE *m = AllocVec((ULONG)h * bc, MEMF_CHIP);

    if (m)
        for (LONG i = 0; i < (LONG)h * bc; i++)
            m[i] = 0xFF;

    return m;
}

// --- drawmodes --------------------------------------------------------------

// Pens with their bits spread across the byte, so a write mask that keeps only
// some bit positions lands on a value distinct from both operands and from the
// backgrounds -- the same choice RectFill makes, for the same reason.
#define FG 0x5A
#define BG 0xA5
#define SCENE_BG 0x35
#define BAND_BG 0xC6

#define COLS 8

// The eight mode combinations, in the order RectFill and P96Tests render them.
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

static const UBYTE MASKS[] = {0xFF, 0x0F, 0x55, 0x81};
#define NMASKS ((SHORT)(sizeof MASKS / sizeof MASKS[0]))

// One row of the grid: eight patterned tiles at one write mask, over a band of
// a second tone. The band is what makes COMPLEMENT and the JAM2 background pen
// visible -- against a single flat backdrop several of these modes collapse
// onto the same output. The pattern is already set in rp->AreaPtrn, and the
// SetDrMd on every tile is also what keeps the NULL-mask fill re-arming.
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
        BltPattern(rp, NULL, x + c * tile, y, x + (c + 1) * tile - 1,
                   y + tile - 1, 0);
    }
}

// The cross product of the eight draw modes with rp->Mask, each tile filled
// through the pattern rather than solid. Palette only, and permanently: the
// mask selects bitplanes, which a chunky truecolor screen has no equivalent of.
static void t_drawmodes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT tile = w / COLS;
    SHORT pitch = h / NMASKS;
    SHORT x = (w - COLS * tile) / 2;

    if (tile > pitch)
        tile = pitch;

    p96cts_clear(rp, w, h, SCENE_BG);
    if (tile < 4)
        return;

    UWORD *pat = build_pattern();
    if (!pat)
        return;
    SetAfPt(rp, pat, PAT_SIZE);

    for (SHORT r = 0; r < NMASKS; r++)
        mode_row(rp, x, r * pitch + (pitch - tile) / 2, tile, MASKS[r]);

    // Wait for the last blit before dropping the pattern the blitter is
    // reading, and leave AreaPtrn clear so it does not follow into other tests.
    WaitBlit();
    SetAfPt(rp, NULL, 0);
    FreeVec(pat);
}

// --- mask -------------------------------------------------------------------

// A right triangle: row r sets columns 0..r*MW/MH. Asymmetric top to bottom,
// and its hypotenuse turns a wrong row stride into a visibly skewed edge. Chip
// memory for the same blitter reason as the pattern.
static UBYTE *build_triangle(SHORT mw, SHORT mh, SHORT bytecnt) {
    UBYTE *m = AllocVec((ULONG)mh * bytecnt, MEMF_CHIP | MEMF_CLEAR);

    if (!m)
        return NULL;

    for (SHORT r = 0; r < mh; r++) {
        SHORT last = (SHORT)((LONG)r * mw / mh);

        for (SHORT c = 0; c <= last && c < mw; c++)
            m[r * bytecnt + c / 8] |= (UBYTE)(0x80 >> (c & 7));
    }

    return m;
}

// The mask is what separates BltPattern from RectFill, so the scene puts the
// two panels side by side: the same patterned fill cookie-cut to the triangle
// on the left, and to an all-ones mask (the whole rectangle) on the right. JAM2,
// so the pattern's clear bits are painted in BgPen and the cut shows against
// the background rather than only where the pattern happens to be set. A driver
// that ignores the mask fills the left panel solid like the right; one that
// gets bytecnt or the mask origin wrong skews or shifts the triangle.
static void t_mask(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT mw = (w / 2 - 8) & ~15;  // multiple of 16: a whole-word mask row
    SHORT mh = h - 8;
    SHORT bytecnt = mask_bytes(mw);
    SHORT x0 = (w / 2 - mw) / 2;
    SHORT x1 = w / 2 + x0;
    SHORT y0 = (h - mh) / 2;

    p96cts_clear(rp, w, h, 3);
    if (mw < 16 || mh < 8)
        return;

    UBYTE *tri = build_triangle(mw, mh, bytecnt);
    UBYTE *full = solid_mask(mw, mh);
    UWORD *pat = build_pattern();
    if (!tri || !full || !pat) {
        FreeVec(tri);
        FreeVec(full);
        FreeVec(pat);
        return;
    }
    SetAfPt(rp, pat, PAT_SIZE);
    SetDrMd(rp, JAM2);
    SetAPen(rp, 1);
    SetBPen(rp, 2);

    BltPattern(rp, tri, x0, y0, x0 + mw - 1, y0 + mh - 1, bytecnt);
    BltPattern(rp, full, x1, y0, x1 + mw - 1, y0 + mh - 1, bytecnt);

    WaitBlit();
    SetAfPt(rp, NULL, 0);
    FreeVec(pat);
    FreeVec(tri);
    FreeVec(full);
}

// --- phase ------------------------------------------------------------------

// The pattern is anchored so that separate fills tile seamlessly. The scene
// fills a region as a grid of abutting rectangles of the same pattern in JAM1:
// if the phase is continuous the diagonal runs unbroken across every seam, and
// if a driver restarts it at each rectangle the diagonal steps at the seams.
// The tiles are 37 by 31 -- odd, so coprime with the pattern's 8- and 16-pixel
// periods, and a per-rectangle phase cannot line up with the seamless one by
// accident. Each tile fills through an all-ones mask, since a NULL mask would
// not re-arm between same-mode fills; a few dozen tiles keep the run quick while
// still crossing the pattern period at every seam.
static void t_phase(struct RastPort *rp, SHORT w, SHORT h) {
    const SHORT tw = 37, th = 31;
    SHORT x0 = 4, y0 = 4;
    SHORT cols = (w - 2 * x0) / tw;
    SHORT rows = (h - 2 * y0) / th;
    SHORT bytecnt = mask_bytes(tw);

    p96cts_clear(rp, w, h, 3);
    if (cols < 2 || rows < 2)
        return;

    UWORD *pat = build_pattern();
    UBYTE *full = solid_mask(tw, th);
    if (!pat || !full) {
        FreeVec(pat);
        FreeVec(full);
        return;
    }
    SetAfPt(rp, pat, PAT_SIZE);
    SetDrMd(rp, JAM1);
    SetAPen(rp, 1);

    for (SHORT r = 0; r < rows; r++)
        for (SHORT c = 0; c < cols; c++) {
            SHORT x = x0 + c * tw, y = y0 + r * th;
            BltPattern(rp, full, x, y, x + tw - 1, y + th - 1, bytecnt);
        }

    WaitBlit();
    SetAfPt(rp, NULL, 0);
    FreeVec(pat);
    FreeVec(full);
}

static const struct P96Test TESTS[] = {
    {"drawmodes", t_drawmodes, true},
    {"mask", t_mask, false},
    {"phase", t_phase, false},
};

const struct P96TestGroup BltPatternGroup = {
    "BltPattern", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
