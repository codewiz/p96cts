// SPDX-License-Identifier: 0BSD
//
// Everything that talks to graphics.library on the harness's behalf: the color
// helpers testcases draw through, and the readback that turns a rendered scene
// into the bytes the comparison works on.
//
// Split out of main.c, which is left with argument parsing and the run loop.
// Anything that needs an RTG library rather than graphics.library is in rtg.c,
// and the display database is in modes.c.

#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>

#include "gfx.h"
#include "rtg.h"

// --- colors -----------------------------------------------------------------

bool p96cts_truecolor;

// JAM1-fill a rectangle in the given color: a pen number on a palette screen,
// 0x00RRGGBB on truecolor. Corners are inclusive and callers must pass them
// sorted -- both RectFill() and the RTG fills require min <= max, and none of
// them defines what a reversed rectangle does.
void p96cts_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color) {
    if (p96cts_truecolor) {
        rtg_fill_rgb(rp, x1, y1, x2, y2, color);
        return;
    }
    // BgPen is passed back unchanged: JAM1 ignores it, and a fill has no
    // business disturbing the pen a caller set for its own drawing.
    SetABPenDrMd(rp, color, rp->BgPen, JAM1);
    RectFill(rp, x1, y1, x2, y2);
}

// Fill the whole scene with one color, as above. 0 is black either way.
void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color) {
    p96cts_fill(rp, 0, 0, w - 1, h - 1, color);
}

// --- readback ---------------------------------------------------------------

// Read the scene back as one pen per pixel, into a freshly AllocVec'd buffer
// the caller FreeVec's, or NULL. Needs a bitmap addressed by pen.
//
// ReadPixelArray8 works in 16-pixel granules, so w must be a multiple of 16;
// that is where the screen-width constraint on a run comes from.
UBYTE *p96cts_read_pens(struct RastPort *rp, SHORT w, SHORT h, int depth) {
    struct RastPort temprp = *rp;
    UBYTE *idx = AllocVec((ULONG)w * h, MEMF_ANY);

    if (!idx)
        return NULL;

    temprp.Layer = NULL;
    temprp.BitMap = AllocBitMap(w, 1, depth, 0, rp->BitMap);
    if (!temprp.BitMap) {
        FreeVec(idx);
        return NULL;
    }

    ReadPixelArray8(rp, 0, 0, w - 1, h - 1, idx, &temprp);
    FreeBitMap(temprp.BitMap);
    return idx;
}
