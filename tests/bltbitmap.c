// SPDX-License-Identifier: 0BSD
//
// BltBitMapRastPort() testcases: a planar source onto an RTG screen.
//
// The one path in the suite where the source is neither the RastPort's own
// bitmap nor a 1-bit template, but a real multi-plane Amiga BitMap in chip
// memory. P96 routes it to the driver's BlitPlanar2Chunky on a CLUT screen and
// BlitPlanar2Direct on a truecolor one, two hooks nothing else here reaches --
// yet every icon, DrawImage() and planar picture that lands on an RTG screen
// goes through them.
//
// They are also the hooks with the most work to do. The driver has to combine
// the planes into pixels itself, shift the source to the destination alignment,
// apply a minterm and a source plane mask, and on truecolor map each source pen
// through a ColorIndexMapping on the way. Nothing else in the suite passes a
// minterm other than 0xC0 at all.
//
// The scene shapes follow P96Tests/BlitPlanar2Chunky.c (0BSD, by iComp): a
// small source blitted at every minterm and at several widths. The sentinel
// planes above the source depth in the shallow scene are its idea too, kept
// because it catches a driver that reads bm->Planes past bm->Depth -- filled
// with ones rather than left as a bad pointer, so an over-read shows up as
// wrong pixels instead of taking the machine down.
//
// The source is the same P96 wordmark the BltTemplate scenes use, one pen per
// glyph, so a diff image says which way the blit went wrong rather than only
// that it did. The exception is the plane mask scene, which needs more pens
// than a picture has.

#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <string.h>

#include "p96cts.h"
#include "gfx.h"
#include "glyph.h"

// The shared glyph as a picture with real pens rather than a one-bit mask: 48
// wide, three source words, 16 rows of which most scenes blit 8, leaving rows
// below for a source y offset to reach.
#define SRC_W P96CTS_GLYPH_W
#define SRC_H P96CTS_GLYPH_H
#define SRC_MOD P96CTS_GLYPH_MOD

// Every pen instead of the glyph's four, for the one scene that needs them:
// adjacent pixels differ in several bits in both axes and no row repeats, so
// every bit position carries independent information. Unreadable as a picture,
// which is why only the plane mask scene uses it.
//
// A few pixels land on pens the palette cannot distinguish -- pens 4 and 192
// share a color, the grays have red equal to blue -- which costs those pixels
// and no others, the comparison being pixel-exact.
static UBYTE hash_pen(SHORT x, SHORT y) {
    return (UBYTE)(x * 37 + y * 61);
}

// The two checkerboard tones. Dark and close to neutral, both of them, so the
// wordmark's red, green and blue stay readable on top -- a bright or saturated
// background fights them. The pair is still far enough apart in luminance for a
// blit that lands a pixel or a word off to be visible crossing a cell boundary,
// and their bits differ in every nibble so a minterm mixing source and
// destination lands on values distinct from both.
#define CELL_DARK 0x09  // (36, 36, 0), near-black olive
#define CELL_LIGHT 0x52 // (72, 72, 85), dark slate

// A two-tone checkerboard on the same 16-pixel period as the word alignment
// the driver shifts against, so a blit a word or a pixel off crosses a cell
// boundary instead of hiding inside flat color.
static void checker(struct RastPort *rp, SHORT w, SHORT h, SHORT cell) {
    for (SHORT y = 0; y < h; y += cell)
        for (SHORT x = 0; x < w; x += cell) {
            SHORT x2 = x + cell - 1, y2 = y + cell - 1;
            int pen = ((x / cell + y / cell) & 1) ? CELL_DARK : CELL_LIGHT;

            // gfx_fill, not SetAPen and RectFill: on truecolor SetAPen
            // takes a pen number, so it would keep only the low byte of the
            // color and fill with whatever pen that is.
            gfx_fill(rp, x, y, x2 < w ? x2 : w - 1, y2 < h ? y2 : h - 1,
                        gfx_pen(pen));
        }
}

// One minterm, blitted four ways across a row 8 pixels high: the three widths
// P96Tests uses -- one narrower than a word, one exactly a word, one spanning
// all three -- then the full width again at an odd source offset, an odd source
// row and an odd destination x, so no two of the four agree on alignment.
//
// Source and destination alignment vary independently, because a driver that
// derives its shift from the wrong one of the two passes whenever they agree.
#define ROW_W 130

static void blit_row(struct RastPort *rp, struct BitMap *src, SHORT x, SHORT y,
                     UBYTE minterm) {
    BltBitMapRastPort(src, 0, 0, rp, x, y, 8, 8, minterm);
    BltBitMapRastPort(src, 0, 0, rp, x + 12, y, 16, 8, minterm);
    BltBitMapRastPort(src, 0, 0, rp, x + 32, y, SRC_W, 8, minterm);
    BltBitMapRastPort(src, 3, 5, rp, x + 85, y, SRC_W - 3, 8, minterm);
}

// All sixteen minterms, eight to a half of the scene.
//
// Only the four terms with A set are swept, in steps of 0x10: BltBitMap() has
// no A source, so those four bits are the whole truth table over source and
// destination, and 0x00 through 0xF0 are exactly the sixteen boolean functions
// of the two -- FALSE, NOR, ONLYDST, ... , OR, TRUE. This is the same sweep
// P96Tests runs, and the only place in the suite where the minterm is not a
// plain source copy.
static void t_minterms(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT rows = 8, band = h / rows, half = w / 2;
    struct GlyphPlanar s;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    // The RastPort has no Layer, so nothing clips a blit that runs past the
    // bitmap; a scene too small for the grid draws nothing instead.
    if (band < 10 || half < ROW_W + 4)
        return;

    checker(rp, w, h, 16);

    if (!glyph_planar(&s, 8, glyph_pen)) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT r = 0; r < rows; r++) {
        SHORT y = r * band + (band - 8) / 2;

        blit_row(rp, &s.bm, 4, y, (UBYTE)(r << 4));
        blit_row(rp, &s.bm, half + 2, y, (UBYTE)((r + 8) << 4));
    }

    glyph_free_planar(&s);
}

// A 4x4 grid of 16-wide plain copies, one per source x offset 0..15, over the
// checkerboard. Unlike BltTemplate the source offset here is a pixel offset
// into a bitmap and any value inside it is legal, but 0..15 is what matters:
// past that the driver advances by whole words and repeats the same sixteen
// shifts. The destination steps by 5, coprime with 16, so it visits all
// sixteen destination alignments against them.
static void t_offsets(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT cw = w / 4, ch = h / 4;
    struct GlyphPlanar s;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (cw < 34 || ch < 10)
        return;

    checker(rp, w, h, 16);

    if (!glyph_planar(&s, 8, glyph_pen)) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT i = 0; i < 16; i++) {
        SHORT dx = (i % 4) * cw + 2 + (SHORT)((i * 5) % 16);
        SHORT dy = (i / 4) * ch + (ch - 8) / 2;

        BltBitMapRastPort(&s.bm, i, 0, rp, dx, dy, 16, 8, 0xC0);
    }

    glyph_free_planar(&s);
}

// Every width and height from 1 up, and a width that is neither a multiple of
// 16 nor of 8 at every source offset. A driver that rounds the width up to a
// word paints past the right edge of its rectangle, one that rounds down drops
// the last columns, and either shows here as a ragged staircase instead of a
// clean one.
static void t_sizes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT step = w / 16, band = h / 3;
    struct GlyphPlanar s;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (step < 18 || band < 18)
        return;

    checker(rp, w, h, 16);

    if (!glyph_planar(&s, 8, glyph_pen)) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT i = 0; i < 16; i++) {
        SHORT x = i * step + 1;

        BltBitMapRastPort(&s.bm, 0, 0, rp, x, band / 4, i + 1, 8, 0xC0);
        BltBitMapRastPort(&s.bm, 0, 0, rp, x, band + band / 4, 8, i + 1, 0xC0);
        BltBitMapRastPort(&s.bm, i, 0, rp, x, 2 * band + band / 4, 13, 8, 0xC0);
    }

    glyph_free_planar(&s);
}

// The source plane mask: the planes it clears do not reach the destination, so
// the pen written is the source pen with those bits dropped. Distinct from the
// rp->Mask the template and pattern scenes sweep, which protects destination
// bitplanes -- this one selects planes of the source, which exist whatever
// format the destination is, so the scene means the same thing on a truecolor
// screen. There the surviving pen is mapped through the ColorIndexMapping, and
// a driver that masks after the mapping instead of before gets a different
// color.
//
// BltBitMap() rather than BltBitMapRastPort(), which has no plane-mask
// argument. The destination is the RastPort's own bitmap either way.
//
// The one scene on the hash source rather than the wordmark. A mask has to be
// checked bit by bit, and the wordmark carries four pens: masks that differ
// only in a bit none of the four sets would compare equal however the driver
// applied them.
static const UBYTE MASKS[] = {0xFF, 0x0F, 0x55, 0x81};
#define NMASKS ((SHORT)(sizeof MASKS / sizeof MASKS[0]))

static void t_planemask(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT band = h / NMASKS;
    struct GlyphPlanar s;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (band < 10 || w < 2 * SRC_W)
        return;

    checker(rp, w, h, 16);

    if (!glyph_planar(&s, 8, hash_pen)) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT r = 0; r < NMASKS; r++) {
        SHORT y = r * band + (band - 8) / 2;

        // Two minterms per mask: a plain copy, where a dropped plane shows as
        // a changed pen, and EOR, where the mask has to be applied to the
        // combination rather than to the source alone.
        for (SHORT i = 0; i + SRC_W <= w - 4; i += SRC_W + 6)
            BltBitMap(&s.bm, 0, 0, rp->BitMap, i + 2, y, SRC_W, 8,
                      (i / (SRC_W + 6)) & 1 ? 0x60 : 0xC0, MASKS[r], NULL);
    }

    glyph_free_planar(&s);
}

// A stencil blit through BltMaskBitMapRastPort(), the call Workbench icons and
// DrawImage() reach the board with: the source is painted only where the mask
// plane has a bit set, and the destination survives everywhere else.
//
// Only two minterms are defined for it -- ABC|ABNC|ANBC to copy the source and
// ANBC to invert it -- so those are the two the scene uses; the rest of the
// sixteen are the minterms sweep's business, not this one's.
//
// The mask is as wide and high as the source, which is what the autodoc asks
// for, and in chip memory, which it insists on. A diagonal edge with a notch
// in it, so a mask read a word or a row off leaves a step in the outline, and
// asymmetric in both axes so a flipped mask cannot look right.
static PLANEPTR build_stencil(void) {
    PLANEPTR m = AllocRaster(SRC_W, SRC_H);

    if (!m)
        return NULL;
    memset(m, 0, (size_t)SRC_MOD * SRC_H);

    for (SHORT y = 0; y < SRC_H; y++)
        for (SHORT x = 0; x < SRC_W; x++) {
            bool set = x >= y && x < SRC_W - y / 2;

            if (y >= 4 && y < 7 && x >= 12 && x < 20)
                set = false; // the notch
            if (set)
                m[y * SRC_MOD + x / 8] |= (UBYTE)(0x80 >> (x % 8));
        }

    return m;
}

static void t_stencil(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT band = h / 4;
    struct GlyphPlanar s;
    PLANEPTR stencil;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (band < SRC_H + 2 || w < 2 * SRC_W)
        return;

    checker(rp, w, h, 16);

    if (!glyph_planar(&s, 8, glyph_pen)) {
        glyph_free_planar(&s);
        return;
    }
    if (!(stencil = build_stencil())) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT r = 0; r < 4; r++) {
        SHORT y = r * band + (band - SRC_H) / 2;
        // The copy minterm on the first two bands, the invert minterm on the
        // last two, each at an even and an odd destination x.
        UBYTE minterm = r < 2 ? 0xE0 : 0x20;

        for (SHORT i = 0; i + SRC_W <= w - 4; i += SRC_W + 6)
            BltMaskBitMapRastPort(&s.bm, 0, 0, rp, i + 2 + (r & 1), y, SRC_W,
                                  SRC_H, minterm, stencil);
    }

    WaitBlit();
    FreeRaster(stencil, SRC_W, SRC_H);
    glyph_free_planar(&s);
}

// A source shallower than the screen, which is what real planar imagery is: a
// depth-3 icon onto a depth-8 or truecolor screen. On a CLUT screen the blit
// covers the three planes the source has and the destination keeps the other
// five, so the high bits of each pen survive from the background -- a driver
// that clears them paints pens the source never named. On truecolor there are
// no destination planes to keep and the eight source pens simply map through
// the ColorIndexMapping, which is a shorter mapping table than any other scene
// hands it.
//
// Either way the scene traps a driver that reads bm->Planes past bm->Depth:
// the planes above three are all ones, so an over-read forces bits nothing in
// the source could have set.
//
// The background is banded rather than flat so the surviving high bits differ
// down the scene, and each band is blitted at a plain copy and at OR, which
// cannot be told apart over a zero background.
//
// The band pens leave their low three bits clear, which is where the source
// writes, and keep the rest dark for the same readability reason the
// checkerboard does. The first band is zero, so one band shows the source pens
// with nothing of the destination mixed in.
static void t_shallow(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT band = h / 4;
    static const UBYTE BANDS[] = {0x00, 0x10, 0x48, 0x50};
    struct GlyphPlanar s;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (band < SRC_H + 2 || w < 2 * SRC_W)
        return;

    if (!glyph_planar(&s, 3, glyph_pen)) {
        glyph_free_planar(&s);
        return;
    }

    for (SHORT r = 0; r < 4; r++) {
        SHORT y = r * band;

        gfx_fill(rp, 0, y, w - 1, y + band - 1, gfx_pen(BANDS[r]));
    }

    for (SHORT r = 0; r < 4; r++) {
        SHORT y = r * band + (band - SRC_H) / 2;

        for (SHORT i = 0; i + SRC_W <= w - 4; i += SRC_W + 6)
            BltBitMapRastPort(&s.bm, 0, 0, rp, i + 2, y, SRC_W, SRC_H,
                              (i / (SRC_W + 6)) & 1 ? 0xE0 : 0xC0);
    }

    glyph_free_planar(&s);
}

static const struct P96Test TESTS[] = {
    {"minterms", t_minterms, false},
    {"offsets", t_offsets, false},
    {"sizes", t_sizes, false},
    {"planemask", t_planemask, false},
    {"stencil", t_stencil, false},
    {"shallow", t_shallow, false},
};

const struct P96TestGroup BltBitMapGroup = {
    "BltBitMap", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
