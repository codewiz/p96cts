// SPDX-License-Identifier: 0BSD
//
// Spare-bit testcases.
//
// Several RTG formats have bits that carry no color: the alpha byte of the
// four-byte layouts, which libraries/Picasso96.h calls "A unused alpha
// channel", and the top bit of the 15-bit ones, which it writes as a literal
// 0 in 0rrrrrgggggbbbbb. Unused means unused -- nothing promises what a driver
// stores there -- so these scenes assert the one thing that is promised: the
// visible color must not depend on them.
//
// Each scene draws the same bars twice, once from pixels with every spare bit
// clear and once with every spare bit set. The two halves must come out
// identical, which is what the golden records. A driver that lets a spare bit
// reach the visible color, or that compares pixels at full width where it
// should mask, produces two different halves and fails.

#include <graphics/rastport.h>
#include <stddef.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "p96cts.h"
#include "countof.h"
#include "gfx.h"
#include "rtg.h"

// Colors chosen to survive the trip through a 15-bit screen unchanged: every
// component is a multiple of 8 with its low three bits mirrored from the high
// three, which is what a 5-bit gun expands back to.
static const UBYTE BARS[][3] = {
    {0xff, 0x00, 0x00}, {0x00, 0xff, 0x00}, {0x00, 0x00, 0xff},
    {0xff, 0xff, 0x00}, {0x00, 0xff, 0xff}, {0xff, 0x00, 0xff},
    {0xff, 0xff, 0xff}, {0x08, 0x08, 0x08}, {0x84, 0x84, 0x84},
    {0xce, 0x63, 0x21},
};
#define NBARS ((int)countof(BARS))

// One half of the scene: NBARS vertical bars filling w x h, composed in `fmt`
// with every spare bit set to `spare`. Returns the buffer, or NULL.
static UBYTE *compose_bars(ULONG fmt, SHORT w, SHORT h, bool spare,
                           SHORT *stride) {
    const int bpp = rtg_format_bytes(fmt);
    UBYTE *px;

    if (bpp == 0)
        return NULL;
    *stride = (SHORT)(w * bpp);
    px = (UBYTE *)AllocVec((ULONG)*stride * h, MEMF_ANY);
    if (!px)
        return NULL;

    for (SHORT y = 0; y < h; y++)
        for (SHORT x = 0; x < w; x++) {
            // Bars run vertically, and a horizontal seam every 16 rows turns
            // each bar into blocks, so a blit that repeats one row shows up
            // as a bar with no seams rather than as the right color.
            const int bar = (x * NBARS) / w;
            const int shade = ((y >> 4) & 1) ? 1 : 0;
            const UBYTE r = (UBYTE)(BARS[bar][0] >> shade);
            const UBYTE g = (UBYTE)(BARS[bar][1] >> shade);
            const UBYTE b = (UBYTE)(BARS[bar][2] >> shade);

            rtg_compose_pixel(px + (size_t)y * *stride + (size_t)x * bpp, fmt,
                              r, g, b, spare);
        }
    return px;
}

// Write both halves straight to the screen. This is the pixel array path:
// whatever the driver does with the spare bits on the way in, the two halves
// must read back the same.
static void scene_write(struct RastPort *rp, SHORT w, SHORT h) {
    const ULONG fmt = rtg_rgbformat(rp->BitMap);
    const SHORT half = (SHORT)(w / 2);
    SHORT stride = 0;
    UBYTE *px;

    gfx_clear(rp, w, h, gfx_pen(0));

    px = compose_bars(fmt, half, h, false, &stride);
    if (px) {
        rtg_write_native(rp, 0, 0, half, h, px, 0, 0, stride, fmt);
        FreeVec(px);
    }
    px = compose_bars(fmt, half, h, true, &stride);
    if (px) {
        rtg_write_native(rp, half, 0, half, h, px, 0, 0, stride, fmt);
        FreeVec(px);
    }

}

// The same two halves, but each reaches the screen through a BltBitMap() from
// an off-screen friend bitmap rather than directly. A blit that treats the
// spare bits as color -- masking against them, or carrying them into a
// comparison -- diverges from the half beside it.
static void scene_blit(struct RastPort *rp, SHORT w, SHORT h) {
    const ULONG fmt = rtg_rgbformat(rp->BitMap);
    const SHORT half = (SHORT)(w / 2);
    struct BitMap *bm = rtg_alloc_friend(rp->BitMap, half, h);
    struct RastPort src;
    SHORT stride = 0;

    gfx_clear(rp, w, h, gfx_pen(0));
    if (!bm)
        return;
    InitRastPort(&src);
    src.BitMap = bm;

    for (int i = 0; i < 2; i++) {
        UBYTE *px = compose_bars(fmt, half, h, i != 0, &stride);

        if (!px)
            continue;
        rtg_write_native(&src, 0, 0, half, h, px, 0, 0, stride, fmt);
        FreeVec(px);
        BltBitMap(bm, 0, 0, rp->BitMap, (SHORT)(i * half), 0, half, h, 0xc0,
                  0xff, NULL);
    }
    WaitBlit();
    rtg_free_friend(bm);
}

static const struct P96Test TESTS[] = {
    {"write", scene_write, false, true, true, false},
    {"blit", scene_blit, false, true, true, true},
};

const struct P96TestGroup UnusedBitsGroup = {"UnusedBits", TESTS,
                                             (int)countof(TESTS)};
