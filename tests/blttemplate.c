// SPDX-License-Identifier: 0BSD
//
// BltTemplate() testcases.
//
// BltTemplate expands a 1-bit-per-pixel source into the RastPort, painting
// FgPen where a bit is set and, in JAM2, BgPen where it is clear. It is the
// operation text rendering is built on, which is why a driver that gets it
// wrong is so visible in use and so easy to miss in a test that only draws
// rectangles and lines.
//
// The interesting parameter is srcX: a bit offset into the source, not a byte
// or word one, so the driver has to shift the source into place. Drivers
// routinely handle srcX 0 and misplace everything else by a pixel or a word,
// and just as routinely drop the trailing partial word when the width is not a
// multiple of 16. The scenes sweep both, and the offset sweep runs into the
// second source word, which is where P96's software rasterizer and
// graphics.library part ways (see t_offsets).

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"

// 48 wide, three source words: enough to hold the P96 wordmark below, and
// enough for a 16-wide window to start past the first word boundary.
#define TPL_W 48
#define TPL_H 16
#define TPL_MOD (TPL_W / 8) // bytes per source row, what BltTemplate calls the
                            // modulo

// The source, as ASCII so that a reader can see what should end up on screen:
// a P96 wordmark that fills the whole template, top-left pixel to bottom-right.
// Asymmetric in both axes on purpose, so a driver that mirrors or flips the
// source cannot produce the same image -- the three glyphs differ left to
// right, the bowls sit high, and the 6's loop puts solid content in the lower
// right, which is where an offset error shows first. Each glyph is a cell of
// its own (14, 14, 15 wide) with a narrow gap between, so a column of set
// pixels reaches both edges.
static const char *const GLYPH[TPL_H] = {
    "#############." "..." ".############." ".." ".##############",
    "##############" "..." "##############" ".." "###############",
    "###.......####" "..." "###........###" ".." "###............",
    "###........###" "..." "###........###" ".." "###............",
    "###........###" "..." "###........###" ".." "###............",
    "###.......####" "..." "###........###" ".." "###............",
    "##############" "..." "###........###" ".." "##############.",
    "#############." "..." "##############" ".." "###############",
    "###..........." "..." ".#############" ".." "###.........###",
    "###..........." "..." "...........###" ".." "###.........###",
    "###..........." "..." "...........###" ".." "###.........###",
    "###..........." "..." "...........###" ".." "###.........###",
    "###..........." "..." "...........###" ".." "###.........###",
    "###..........." "..." "...........###" ".." "###.........###",
    "###..........." "..." "##############" ".." "###############",
    "###..........." "..." "#############." ".." ".#############.",
};

// Pack the glyph for BltTemplate, most significant bit leftmost.
//
// Chip memory, because on a native planar screen graphics.library runs this
// through the blitter, which cannot see fast RAM -- a template in fast memory
// renders as garbage there while working fine on every RTG board.
static PLANEPTR build_template(void) {
    UBYTE *tpl = AllocVec(TPL_H * TPL_MOD, MEMF_CHIP | MEMF_CLEAR);

    if (!tpl)
        return NULL;

    for (int y = 0; y < TPL_H; y++)
        for (int x = 0; x < TPL_W; x++)
            if (GLYPH[y][x] == '#')
                tpl[y * TPL_MOD + x / 8] |= (UBYTE)(0x80 >> (x % 8));

    return (PLANEPTR)tpl;
}

// A two-tone checkerboard on the same 16-pixel period as the word alignment
// the driver is shifting against, so a blit that lands a word or a pixel off
// crosses a cell boundary instead of hiding in flat color.
static void checker(struct RastPort *rp, SHORT w, SHORT h, SHORT cell) {
    for (SHORT y = 0; y < h; y += cell)
        for (SHORT x = 0; x < w; x += cell) {
            SHORT x2 = x + cell - 1, y2 = y + cell - 1;

            SetAPen(rp, ((x / cell + y / cell) & 1) ? 5 : 0);
            RectFill(rp, x, y, x2 < w ? x2 : w - 1, y2 < h ? y2 : h - 1);
        }
}

// A 4x8 grid of 16-wide windows onto the glyph, one per source bit offset
// 0..31, over the checkerboard so the mask is visible: in JAM1 the clear bits
// must leave the background exactly as it was.
//
// Source and destination alignment are varied independently, because a driver
// that derives its shift from the wrong one of the two passes whenever they
// happen to agree. srcX walks 0..31 in order -- crossing from the first source
// word into the second halfway through -- while the destination steps by 5,
// which is coprime with 16 and so visits all sixteen destination alignments.
//
// The second-word half (srcX >= 16) is where P96's software rasterizer, the
// reference this suite captures, disagrees with graphics.library: the z3660
// driver and native AGA render it identically to each other and differently
// from softrast, so softrast looks like the odd one out. The golden comes from
// softrast, so this scene passes there and fails on the z3660 driver and on
// AGA. Whether the fault is in P96 or in the chipset emulation this was measured
// on is unresolved -- either way the sweep keeps going so the discrepancy shows
// up instead of being skipped.
static void t_offsets(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT cw = w / 8, ch = h / 4;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    // The RastPort has no Layer, so nothing clips a blit that runs past the
    // bitmap; a scene too small for the grid draws nothing instead.
    if (cw < 34 || ch < TPL_H + 2)
        return;

    checker(rp, w, h, 16);

    PLANEPTR tpl = build_template();
    if (!tpl)
        return;

    SetAPen(rp, 1);
    for (int i = 0; i < 32; i++) {
        SHORT dx = (i % 8) * cw + 2 + (SHORT)((i * 5) % 16);
        SHORT dy = (i / 8) * ch + (ch - TPL_H) / 2;

        BltTemplate(tpl, i, TPL_MOD, rp, dx, dy, 16, TPL_H);
    }

    FreeVec(tpl);
}

// Every width and height from 1 up, and a width that is neither a multiple of
// 16 nor of 8 at every source offset. A driver that rounds the width up to a
// word paints past the right edge of its rectangle, one that rounds down drops
// the last few columns, and either shows here as a ragged staircase instead of
// a clean one.
static void t_sizes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT step = w / 16, band = h / 3;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    // The widest blit here is 16, so tiles need only that much room, not the
    // full template width.
    if (step < 18 || band < TPL_H + 2)
        return;

    checker(rp, w, h, 16);

    PLANEPTR tpl = build_template();
    if (!tpl)
        return;

    SetAPen(rp, 1);
    for (SHORT i = 0; i < 16; i++) {
        SHORT x = i * step + 1;

        // Widths 1..16 at full height, then heights 1..16 at a fixed width,
        // then a 13-wide slice at each source offset in turn.
        BltTemplate(tpl, 0, TPL_MOD, rp, x, band / 4, i + 1, TPL_H);
        BltTemplate(tpl, 0, TPL_MOD, rp, x, band + band / 4, 8, i + 1);
        BltTemplate(tpl, i, TPL_MOD, rp, x, 2 * band + band / 4, 13, TPL_H);
    }

    FreeVec(tpl);
}

// The draw modes, each over two backgrounds so that no two of them can produce
// the same output. JAM1 leaves the clear bits alone, JAM2 paints them in BgPen,
// INVERSVID swaps the roles of set and clear, and COMPLEMENT ignores both pens
// and inverts the destination -- under the set bits on its own, under the clear
// bits when INVERSVID is added.
static const UBYTE MODES[] = {
    JAM1,       JAM2,       JAM1 | INVERSVID,
    JAM2 | INVERSVID, COMPLEMENT, COMPLEMENT | INVERSVID,
};
#define NMODES ((SHORT)(sizeof MODES / sizeof MODES[0]))

static void t_drawmodes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT cw = w / NMODES;

    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (cw < TPL_W + 2 || h < 2 * TPL_H + 4)
        return;

    // Two bands rather than one flat background: COMPLEMENT and the JAM2
    // background pen are indistinguishable from JAM1 against the wrong tone.
    SetAPen(rp, 5);
    RectFill(rp, 0, 0, w - 1, h / 2 - 1);
    SetAPen(rp, 3);
    RectFill(rp, 0, h / 2, w - 1, h - 1);

    PLANEPTR tpl = build_template();
    if (!tpl)
        return;

    SetAPen(rp, 1);
    SetBPen(rp, 2);
    for (SHORT i = 0; i < NMODES; i++) {
        SHORT x = i * cw + (cw - TPL_W) / 2;

        SetDrMd(rp, MODES[i]);
        BltTemplate(tpl, 0, TPL_MOD, rp, x, h / 4 - TPL_H / 2, TPL_W, TPL_H);
        BltTemplate(tpl, 0, TPL_MOD, rp, x, 3 * h / 4 - TPL_H / 2, TPL_W,
                    TPL_H);
    }

    SetDrMd(rp, JAM1);
    SetBPen(rp, 0);
    FreeVec(tpl);
}

// rp->Mask restricts the write to the selected bitplanes; the unselected ones
// keep the destination. The driver's BlitTemplate hook has its own mask path,
// distinct from RectFill's, so a driver can get one right and the other wrong.
// Both pens have their bits spread across the byte, and the background does too,
// so a mask that drops some planes lands on a value distinct from FgPen, BgPen
// and the background alike. Planar only: the mask selects bitplanes, which a
// chunky truecolor screen has no equivalent of.
static const UBYTE MASKS[] = {0xFF, 0x0F, 0x55, 0x81};
#define NMASKS ((SHORT)(sizeof MASKS / sizeof MASKS[0]))

static void t_masks(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT band = h / NMASKS;

    // A spread-bit background, so the planes a mask protects are visible in the
    // result rather than reading back as zero.
    p96cts_clear(rp, w, h, 0x3C);

    if (band < TPL_H + 2 || w < 2 * TPL_W)
        return;

    PLANEPTR tpl = build_template();
    if (!tpl)
        return;

    // JAM2, so the mask is applied to the BgPen write on the clear bits too,
    // not only the FgPen write on the set bits.
    SetDrMd(rp, JAM2);
    SetAPen(rp, 0x5A);
    SetBPen(rp, 0xA5);
    for (SHORT r = 0; r < NMASKS; r++) {
        SHORT y = r * band + (band - TPL_H) / 2;

        rp->Mask = MASKS[r];
        for (SHORT x = 8; x + TPL_W <= w; x += TPL_W + 12)
            BltTemplate(tpl, 0, TPL_MOD, rp, x, y, TPL_W, TPL_H);
    }

    rp->Mask = 0xFF;
    SetDrMd(rp, JAM1);
    SetBPen(rp, 0);
    FreeVec(tpl);
}

static const struct P96Test TESTS[] = {
    {"offsets", t_offsets, false},
    {"sizes", t_sizes, false},
    {"drawmodes", t_drawmodes, false},
    {"masks", t_masks, true},
};

const struct P96TestGroup BltTemplateGroup = {
    "BltTemplate", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
