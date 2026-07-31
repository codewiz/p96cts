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
// multiple of 16. The scenes sweep both, staying within the srcX 0..15 the
// autodoc allows: a 16-wide window at srcX 15 still reads into the second
// source word, so the word boundary is covered without an out-of-range offset.

#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "gfx.h"
#include "glyph.h"

// The shared glyph: 48 wide, three source words, which is also enough for a
// 16-wide window at srcX 15 to read across the first word boundary. TPL_MOD is
// what BltTemplate calls the modulo.
#define TPL_W P96CTS_GLYPH_W
#define TPL_H P96CTS_GLYPH_H
#define TPL_MOD P96CTS_GLYPH_MOD

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

// A 4x4 grid of 16-wide windows onto the glyph, one per source bit offset
// 0..15, over the checkerboard so the mask is visible: in JAM1 the clear bits
// must leave the background exactly as it was.
//
// Source and destination alignment are varied independently, because a driver
// that derives its shift from the wrong one of the two passes whenever they
// happen to agree. srcX walks 0..15 -- every sub-word bit offset the caller is
// allowed to pass -- while the destination steps by 5, which is coprime with 16
// and so visits all sixteen destination alignments.
//
// The BltTemplate autodoc fixes srcX at 0..15: the source pointer names the
// word containing the mask and srcX does the fine alignment inside it, with the
// pointer advanced by whole words for anything beyond. srcX >= 16 is therefore
// undefined -- drivers disagree there and none is wrong -- so the sweep stops at
// 15. The second source word is still exercised: a 16-wide window at srcX 15
// reads through bit 30, across the word boundary, without an out-of-range offset.
static void t_offsets(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT cw = w / 4, ch = h / 4;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    // The RastPort has no Layer, so nothing clips a blit that runs past the
    // bitmap; a scene too small for the grid draws nothing instead.
    if (cw < 34 || ch < TPL_H + 2)
        return;

    checker(rp, w, h, 16);

    PLANEPTR tpl = glyph_template();
    if (!tpl)
        return;

    SetAPen(rp, 1);
    for (int i = 0; i < 16; i++) {
        SHORT dx = (i % 4) * cw + 2 + (SHORT)((i * 5) % 16);
        SHORT dy = (i / 4) * ch + (ch - TPL_H) / 2;

        BltTemplate(tpl, i, TPL_MOD, rp, dx, dy, 16, TPL_H);
    }

    glyph_free_template(tpl);
}

// Every width and height from 1 up, and a width that is neither a multiple of
// 16 nor of 8 at every source offset. A driver that rounds the width up to a
// word paints past the right edge of its rectangle, one that rounds down drops
// the last few columns, and either shows here as a ragged staircase instead of
// a clean one.
static void t_sizes(struct RastPort *rp, SHORT w, SHORT h) {
    SHORT step = w / 16, band = h / 3;

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    // The widest blit here is 16, so tiles need only that much room, not the
    // full template width.
    if (step < 18 || band < TPL_H + 2)
        return;

    checker(rp, w, h, 16);

    PLANEPTR tpl = glyph_template();
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

    glyph_free_template(tpl);
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

    gfx_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);

    if (cw < TPL_W + 2 || h < 2 * TPL_H + 4)
        return;

    // Two bands rather than one flat background: COMPLEMENT and the JAM2
    // background pen are indistinguishable from JAM1 against the wrong tone.
    SetAPen(rp, 5);
    RectFill(rp, 0, 0, w - 1, h / 2 - 1);
    SetAPen(rp, 3);
    RectFill(rp, 0, h / 2, w - 1, h - 1);

    PLANEPTR tpl = glyph_template();
    if (!tpl)
        return;

    for (SHORT i = 0; i < NMODES; i++) {
        SHORT x = i * cw + (cw - TPL_W) / 2;

        SetABPenDrMd(rp, 1, 2, MODES[i]);
        BltTemplate(tpl, 0, TPL_MOD, rp, x, h / 4 - TPL_H / 2, TPL_W, TPL_H);
        BltTemplate(tpl, 0, TPL_MOD, rp, x, 3 * h / 4 - TPL_H / 2, TPL_W,
                    TPL_H);
    }

    glyph_free_template(tpl);
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
    gfx_clear(rp, w, h, 0x3C);

    if (band < TPL_H + 2 || w < 2 * TPL_W)
        return;

    PLANEPTR tpl = glyph_template();
    if (!tpl)
        return;

    // JAM2, so the mask is applied to the BgPen write on the clear bits too,
    // not only the FgPen write on the set bits.
    SetABPenDrMd(rp, 0x5A, 0xA5, JAM2);
    for (SHORT r = 0; r < NMASKS; r++) {
        SHORT y = r * band + (band - TPL_H) / 2;

        rp->Mask = MASKS[r];
        for (SHORT x = 8; x + TPL_W <= w; x += TPL_W + 12)
            BltTemplate(tpl, 0, TPL_MOD, rp, x, y, TPL_W, TPL_H);
    }

    glyph_free_template(tpl);
}

static const struct P96Test TESTS[] = {
    {.name = "offsets", .fn = t_offsets},
    {.name = "sizes", .fn = t_sizes},
    {.name = "drawmodes", .fn = t_drawmodes},
    {.name = "masks", .fn = t_masks, .palette_only = true},
};

const struct P96TestGroup BltTemplateGroup = {
    "BltTemplate", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
