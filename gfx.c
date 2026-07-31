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

bool gfx_truecolor;

// JAM1-fill a rectangle in the given color: a pen number on a palette screen,
// 0x00RRGGBB on truecolor. Corners are inclusive and callers must pass them
// sorted -- both RectFill() and the RTG fills require min <= max, and none of
// them defines what a reversed rectangle does.
void gfx_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color) {
    if (gfx_truecolor) {
        rtg_fill_rgb(rp, x1, y1, x2, y2, color);
        return;
    }
    // BgPen is passed back unchanged: JAM1 ignores it, and a fill has no
    // business disturbing the pen a caller set for its own drawing.
    SetABPenDrMd(rp, color, rp->BgPen, JAM1);
    RectFill(rp, x1, y1, x2, y2);
}

// Fill the whole scene with one color, as above. 0 is black either way.
void gfx_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color) {
    gfx_fill(rp, 0, 0, w - 1, h - 1, color);
}

// --- readback ---------------------------------------------------------------

// The one-row scratch RastPort both pixel-array calls need, w pixels wide.
// Returns false and leaves nothing allocated on failure.
//
// Built from scratch rather than copied from rp on purpose. The autodoc's
// WritePixelArray8 BUGS section retracts the old advice to copy it: rp->Mask
// interferes with the ClipBlit() these calls are implemented over, which
// corrupts the data. Scenes do leave a mask set -- BltTemplate-masks ends on
// 0x81 -- so a copy would carry it into the readback.
static bool temp_rastport(struct RastPort *temprp, struct BitMap *friend,
                          SHORT w, int depth) {
    InitRastPort(temprp);
    temprp->BitMap = AllocBitMap(w, 1, depth, 0, friend);
    return temprp->BitMap != NULL;
}

static void free_temp_rastport(struct RastPort *temprp) {
    FreeBitMap(temprp->BitMap);
}

// Read a rectangle back as one pen per pixel, into a freshly AllocVec'd buffer
// the caller FreeVec's, or NULL. Needs a bitmap addressed by pen.
//
// ReadPixelArray8 works in 16-pixel granules, so w must be a multiple of 16;
// that is where the screen-width constraint on a run comes from.
UBYTE *gfx_read_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                     int depth) {
    struct RastPort temprp;
    UBYTE *idx = AllocVec((ULONG)w * h, MEMF_ANY);

    if (!idx)
        return NULL;

    if (!temp_rastport(&temprp, rp->BitMap, w, depth)) {
        FreeVec(idx);
        return NULL;
    }

    ReadPixelArray8(rp, x0, y0, x0 + w - 1, y0 + h - 1, idx, &temprp);
    free_temp_rastport(&temprp);
    return idx;
}

// The other direction: w x h pens from px into the rectangle at (x0, y0).
//
// WritePixelArray8 converts the array in place and is documented to destroy it,
// so callers must not expect px back intact.
void gfx_write_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                    UBYTE *px, int depth) {
    struct RastPort temprp;

    if (!temp_rastport(&temprp, rp->BitMap, w, depth))
        return;

    WritePixelArray8(rp, x0, y0, x0 + w - 1, y0 + h - 1, px, &temprp);
    free_temp_rastport(&temprp);
}
