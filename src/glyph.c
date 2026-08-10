// SPDX-License-Identifier: 0BSD
//
// The P96 wordmark, as ASCII so a reader can see what a scene should draw, and
// the two forms scenes blit it in: a one-bit template for BltTemplate and a
// planar BitMap for BltBitMapRastPort. Both readings live here so that a change
// to the art's conventions only has one place to reach.

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>

#include "glyph.h"

// A dot is clear and every other character is set; which character says what
// color, a letter per glyph, so a scene with colors to give can tell the three
// apart and one without can just test for a dot.
//
// The image fills its whole extent, top-left pixel to bottom-right, and is
// asymmetric in both axes on purpose, so a driver that mirrors or flips the
// source cannot produce the same result -- the three glyphs differ left to
// right, the bowls sit high, and the 6's loop puts solid content in the lower
// right, which is where an offset error shows first. Each glyph is a cell of
// its own (14, 14, 15 wide) with a narrow gap between, so a column of set
// pixels reaches both edges.
const char *const glyph[P96CTS_GLYPH_H] = {
    "RRRRRRRRRRRRR." "..." ".GGGGGGGGGGGG." ".." ".BBBBBBBBBBBBBB",
    "RRRRRRRRRRRRRR" "..." "GGGGGGGGGGGGGG" ".." "BBBBBBBBBBBBBBB",
    "RRR.......RRRR" "..." "GGG........GGG" ".." "BBB............",
    "RRR........RRR" "..." "GGG........GGG" ".." "BBB............",
    "RRR........RRR" "..." "GGG........GGG" ".." "BBB............",
    "RRR.......RRRR" "..." "GGG........GGG" ".." "BBB............",
    "RRRRRRRRRRRRRR" "..." "GGG........GGG" ".." "BBBBBBBBBBBBBB.",
    "RRRRRRRRRRRRR." "..." "GGGGGGGGGGGGGG" ".." "BBBBBBBBBBBBBBB",
    "RRR..........." "..." ".GGGGGGGGGGGGG" ".." "BBB.........BBB",
    "RRR..........." "..." "...........GGG" ".." "BBB.........BBB",
    "RRR..........." "..." "...........GGG" ".." "BBB.........BBB",
    "RRR..........." "..." "...........GGG" ".." "BBB.........BBB",
    "RRR..........." "..." "...........GGG" ".." "BBB.........BBB",
    "RRR..........." "..." "...........GGG" ".." "BBB.........BBB",
    "RRR..........." "..." "GGGGGGGGGGGGGG" ".." "BBBBBBBBBBBBBBB",
    "RRR..........." "..." "GGGGGGGGGGGGG." ".." ".BBBBBBBBBBBBB.",
};

// One pen per glyph, on a pen 0 background, so the P, the 9 and the 6 are
// separately identifiable in a diff image and a driver that shifts or mirrors
// the source cannot land on the same colors.
//
// Red, green and blue as far as a 3-3-2 palette gets, but not pens 2, 3 and 4:
// those have almost no bits set, and pen 4 shares its color with pen 192, so a
// driver that wrote the wrong one of the two would compare equal. These three
// have their bits spread instead, and between them cover all eight -- which is
// what a plane mask has to bite on. None is a gray, so a driver that swaps red
// and blue shows up here too.
UBYTE glyph_pen(SHORT x, SHORT y) {
    switch (glyph[y][x]) {
    case 'R': return 0x47; // (252,  36,  85)
    case 'G': return 0xB9; // ( 36, 252, 170)
    case 'B': return 0xC1; // ( 36,   0, 255)
    }
    return 0;
}

// Packed one bit per pixel, most significant bit leftmost, as BltTemplate wants
// it. Color is dropped: a template is a mask, and the caller's pens decide.
//
// Chip memory, because on a native planar screen graphics.library runs this
// through the blitter, which cannot see fast RAM -- a template in fast memory
// renders as garbage there while working fine on every RTG board.
PLANEPTR glyph_template(void) {
    UBYTE *tpl = (UBYTE *)AllocVec(P96CTS_GLYPH_H * P96CTS_GLYPH_MOD,
                          MEMF_CHIP | MEMF_CLEAR);

    if (!tpl)
        return NULL;

    for (SHORT y = 0; y < P96CTS_GLYPH_H; y++)
        for (SHORT x = 0; x < P96CTS_GLYPH_W; x++)
            if (glyph[y][x] != '.')
                tpl[y * P96CTS_GLYPH_MOD + x / 8] |= (UBYTE)(0x80 >> (x % 8));

    return (PLANEPTR)tpl;
}

void glyph_free_template(PLANEPTR tpl) {
    // Wait for the last blit before freeing chip memory the blitter may still
    // be reading.
    WaitBlit();
    FreeVec(tpl);
}

// The same image as a real multi-plane BitMap, which is what reaches a driver's
// BlitPlanar2Chunky and BlitPlanar2Direct hooks. pen_at supplies the pixels, so
// a scene that needs more pens than the three glyphs carry can pass its own.
//
// Built by hand rather than with AllocBitMap() so that every plane pointer is
// ours to set: the planes from depth up to 8 get an all-ones raster, which a
// driver that reads more planes than bm->Depth turns into visibly wrong pens.
// AllocBitMap() would leave them NULL and the same bug would be a crash.
//
// Chip memory throughout, for the same blitter reason as the template.
bool glyph_planar(struct GlyphPlanar *p, int depth,
                         UBYTE (*pen_at)(SHORT x, SHORT y)) {
    InitBitMap(&p->bm, depth, P96CTS_GLYPH_W, P96CTS_GLYPH_H);
    p->ones = NULL;

    for (int i = 0; i < 8; i++)
        p->bm.Planes[i] = NULL;

    for (int i = 0; i < depth; i++) {
        if (!(p->bm.Planes[i] = AllocRaster(P96CTS_GLYPH_W, P96CTS_GLYPH_H)))
            return false;
        memset(p->bm.Planes[i], 0, (size_t)P96CTS_GLYPH_MOD * P96CTS_GLYPH_H);
    }

    if (depth < 8) {
        if (!(p->ones = AllocRaster(P96CTS_GLYPH_W, P96CTS_GLYPH_H)))
            return false;
        memset(p->ones, 0xFF, (size_t)P96CTS_GLYPH_MOD * P96CTS_GLYPH_H);
        for (int i = depth; i < 8; i++)
            p->bm.Planes[i] = p->ones;
    }

    for (SHORT y = 0; y < P96CTS_GLYPH_H; y++)
        for (SHORT x = 0; x < P96CTS_GLYPH_W; x++) {
            UBYTE pen = pen_at(x, y);

            for (int i = 0; i < depth; i++)
                if (pen & (1 << i))
                    p->bm.Planes[i][y * P96CTS_GLYPH_MOD + x / 8] |=
                        (UBYTE)(0x80 >> (x % 8));
        }

    return true;
}

void glyph_free_planar(struct GlyphPlanar *p) {
    WaitBlit();
    for (int i = 0; i < 8; i++)
        if (p->bm.Planes[i] && p->bm.Planes[i] != p->ones)
            FreeRaster(p->bm.Planes[i], P96CTS_GLYPH_W, P96CTS_GLYPH_H);
    if (p->ones)
        FreeRaster(p->ones, P96CTS_GLYPH_W, P96CTS_GLYPH_H);
}
