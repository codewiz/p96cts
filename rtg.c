// SPDX-License-Identifier: 0BSD
//
// Everything that goes through Picasso96API.library or cybergraphics.library,
// and nothing outside this file does.
//
// Two systems answer these questions. On AmigaOS the board is driven by P96,
// which is what this suite tests. AROS has no Picasso96API.library at all and
// answers the same questions through its own cybergraphics.library and
// graphics.library.
//
// The two APIs are not interchangeable in general -- P96 exposes board and
// memory details CGX has no notion of -- but everything this suite asks for
// has a counterpart in both. So the choice is made once at open time, and each
// function here branches on which base is set.

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>

#include "report.h"
#include "rtg.h"

// Either may stay NULL; rtg_open() refuses to return with both unset.
struct Library *P96Base;
struct Library *CyberGfxBase;

// --- libraries --------------------------------------------------------------

bool rtg_open(void) {
    P96Base = OpenLibrary((STRPTR)"Picasso96API.library", 2);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    if (!P96Base && !CyberGfxBase) {
        rpt_errorf("failed to open Picasso96API.library 2 or "
                      "cybergraphics.library 40; one of them is needed");
        return false;
    }
    return true;
}

void rtg_close(void) {
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    if (P96Base)
        CloseLibrary(P96Base);
    CyberGfxBase = NULL;
    P96Base = NULL;
}

// --- the reference bitmap ---------------------------------------------------

// Wider than any P96 board will accept a bitmap, which is how the reference
// allocation is forced into fast memory. P96Tests uses 4100 for the same
// reason.
#define REFERENCE_WIDTH 4100

// Friended to the screen's bitmap, so pens resolve through the screen's colors
// -- but wider than any board can hold, which is what keeps it in fast memory
// anyway. BMF_USERPRIVATE alone does not: with a friend and a width the board
// can take, P96 puts it on the card and the run is no longer an independent
// reference. The trick, and the width, are from P96Tests/DrawLine.c.
//
// Off the board, rtg.library rasterizes this itself: P96's own software
// implementation of the same primitives, independent of any card driver and of
// the blitter. Only the leftmost part is drawn on and read back; the rest is
// there to make the allocation refuse the card.
static struct BitMap *p96_alloc_reference(struct Screen *scr, int height,
                                          int depth) {
    struct BitMap *bm = p96AllocBitMap(REFERENCE_WIDTH, height, depth,
                                       BMF_CLEAR | BMF_USERPRIVATE,
                                       scr->RastPort.BitMap,
                                       depth > 8 ? RGBFB_R8G8B8 : RGBFB_CLUT);

    if (!bm) {
        rpt_errorf("p96AllocBitMap %dx%dx%d failed", REFERENCE_WIDTH, height,
               depth);
        return NULL;
    }
    // Checked rather than assumed from the width, since landing on the board
    // is the one failure that would still produce plausible-looking output.
    if (p96GetBitMapAttr(bm, P96BMA_ISONBOARD)) {
        rpt_errorf("reference bitmap landed on the board; it would not be "
                      "an independent reference");
        p96FreeBitMap(bm);
        return NULL;
    }
    return bm;
}

// The same through graphics.library alone, which is what AROS offers: the
// CyberGraphX pixel format goes in the top 8 bits of the flags rather than in
// an argument of its own, and BMF_SPECIALFMT says it is there.
//
// No ISONBOARD question to ask: without BMF_DISPLAYABLE, AROS allocates a
// plain RAM bitmap and never considers board memory. The width is kept the
// same anyway, so both paths produce a reference of identical geometry.
static struct BitMap *gfx_alloc_reference(struct Screen *scr, int height,
                                          int depth) {
    struct BitMap *bm =
        AllocBitMap(REFERENCE_WIDTH, height, depth,
                    BMF_CLEAR | BMF_SPECIALFMT |
                        SHIFT_PIXFMT(depth > 8 ? PIXFMT_RGB24 : PIXFMT_LUT8),
                    scr->RastPort.BitMap);

    if (!bm)
        rpt_errorf("AllocBitMap %dx%dx%d failed", REFERENCE_WIDTH, height,
                      depth);
    return bm;
}

struct BitMap *rtg_alloc_reference(struct Screen *scr, int height, int depth) {
    return P96Base ? p96_alloc_reference(scr, height, depth)
                   : gfx_alloc_reference(scr, height, depth);
}

// Both take NULL as a no-op, which a driver run always passes, having
// allocated nothing.
void rtg_free_bitmap(struct BitMap *bm) {
    if (P96Base)
        p96FreeBitMap(bm);
    else
        FreeBitMap(bm);
}

// A small bitmap in the same pixel format as `like`, freed with
// rtg_free_friend(). A P96 bitmap needs p96AllocBitMap with its own
// RGBFormat: plain AllocBitMap does not recover a truecolor format from the
// off-board reference bitmap. Off-board friends get off-board copies, so a
// reference run stays driver-independent. Everything else -- planar screens,
// and all of AROS -- is the standard friend-bitmap idiom.
struct BitMap *rtg_alloc_friend(struct BitMap *like, SHORT w, SHORT h) {
    if (P96Base && p96GetBitMapAttr(like, P96BMA_ISP96)) {
        ULONG flags = BMF_CLEAR;

        if (!p96GetBitMapAttr(like, P96BMA_ISONBOARD))
            flags |= BMF_USERPRIVATE;
        return p96AllocBitMap(w, h, p96GetBitMapAttr(like, P96BMA_DEPTH),
                              flags, like,
                              p96GetBitMapAttr(like, P96BMA_RGBFORMAT));
    }
    return AllocBitMap(w, h, GetBitMapAttr(like, BMA_DEPTH), BMF_CLEAR, like);
}

// Branches on the bitmap itself: the friend it was allocated against may be
// gone by free time.
void rtg_free_friend(struct BitMap *bm) {
    if (bm && P96Base && p96GetBitMapAttr(bm, P96BMA_ISP96))
        p96FreeBitMap(bm);
    else
        FreeBitMap(bm);
}

// --- pixel format -----------------------------------------------------------

// The bitmap's pixel format, as an RGBFB_ code. A property of the bitmap,
// never derived from its depth: P96 has three 15-bit formats, three 16-bit,
// two 24-bit and four 32-bit, differing in channel order and byte swapping.
ULONG rtg_rgbformat(struct BitMap *bm) {
    // CGX numbers its formats in its own order, so this is a table and not a
    // cast. PIXFMT_BGR15 and PIXFMT_BGR16 have no P96 counterpart -- P96 named
    // only the byte-swapped forms of those two channel orders -- so they map
    // to RGBFB_NONE and get rejected rather than silently misread.
    static const UBYTE FROM_PIXFMT[] = {
        RGBFB_CLUT,     RGBFB_R5G5B5,   RGBFB_NONE,     RGBFB_R5G5B5PC,
        RGBFB_B5G5R5PC, RGBFB_R5G6B5,   RGBFB_NONE,     RGBFB_R5G6B5PC,
        RGBFB_B5G6R5PC, RGBFB_R8G8B8,   RGBFB_B8G8R8,   RGBFB_A8R8G8B8,
        RGBFB_B8G8R8A8, RGBFB_R8G8B8A8,
    };
    if (P96Base)
        return p96GetBitMapAttr(bm, P96BMA_RGBFORMAT);
    // RGBFB_NONE is P96's name for planar, which is what AGA gives. The name
    // is historical; it is not an error.
    if (!GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX))
        return RGBFB_NONE;

    const ULONG pixfmt = GetCyberMapAttr(bm, CYBRMATTR_PIXFMT);
    if (pixfmt >= sizeof FROM_PIXFMT / sizeof FROM_PIXFMT[0])
        return RGBFB_NONE;
    return FROM_PIXFMT[pixfmt];
}

const char *rtg_format_name(ULONG fmt) {
    static const char *const NAMES[] = {
        "planar",   "clut",     "r8g8b8",   "b8g8r8",   "r5g6b5pc",
        "r5g5b5pc", "a8r8g8b8", "a8b8g8r8", "r8g8b8a8", "b8g8r8a8",
        "r5g6b5",   "r5g5b5",   "b5g6r5pc", "b5g5r5pc", "yuv422cgx",
        "yuv411",   "yuv411pc", "yuv422",   "yuv422pc", "yuv422pa",
        "yuv422papc",
    };
    if (fmt >= (ULONG)(sizeof NAMES / sizeof NAMES[0]))
        return "unknown";
    return NAMES[fmt];
}

// Whether a format is addressed by pen: chunky CLUT on an RTG board, or the
// planar bitmaps AGA gives. ReadPixelArray8 reads either.
bool rtg_format_by_pen(ULONG fmt) {
    return fmt == RGBFB_CLUT || fmt == RGBFB_NONE;
}

// --- truecolor primitives ---------------------------------------------------
//
// The pixel buffers below are R8G8B8, three bytes per pixel, whatever the
// screen's own format is. Converting is the RTG library's job, so a BGRA
// screen and an RGB one produce identical buffers and share one golden set.

// Corners are inclusive and must be sorted; the color is 0x00RRGGBB.
void rtg_fill_rgb(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                  ULONG rgb) {
    if (P96Base) {
        p96RectFill(rp, x1, y1, x2, y2, rgb);
        return;
    }
    // FillPixelArray takes an origin and a size, hence the +1s.
    FillPixelArray(rp, x1, y1, x2 - x1 + 1, y2 - y1 + 1, rgb);
}

// Into a freshly AllocVec'd buffer the caller FreeVec's, or NULL.
UBYTE *rtg_read_rgb(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h) {
    UBYTE *px = AllocVec((ULONG)w * h * 3, MEMF_ANY);

    if (!px)
        return NULL;

    if (P96Base) {
        struct RenderInfo ri;
        ri.Memory = px;
        ri.BytesPerRow = w * 3;
        ri.pad = 0;
        ri.RGBFormat = RGBFB_R8G8B8;
        p96ReadPixelArray(&ri, 0, 0, rp, x0, y0, w, h);
    } else {
        // RECTFMT_RGB is CGX's name for the same layout as RGBFB_R8G8B8.
        ReadPixelArray(px, 0, 0, w * 3, rp, x0, y0, w, h, RECTFMT_RGB);
    }
    return px;
}

// Write w x h pixels into the rectangle at (x0, y0), taking them from (sx, sy)
// within a buffer whose rows are `stride` bytes apart. The source rectangle and
// stride are what let a scene blit a sub-rectangle of a larger image, which is
// the case a driver is most likely to get wrong.
//
// `fmt` picks how px is read: RGBFB_R8G8B8 for three bytes per pixel, or
// RGBFB_CLUT for one pen per byte, which both libraries convert to whatever the
// screen's own format is.
static void write_array(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w,
                        SHORT h, UBYTE *px, SHORT sx, SHORT sy, SHORT stride,
                        ULONG fmt) {
    if (P96Base) {
        struct RenderInfo ri;
        ri.Memory = px;
        ri.BytesPerRow = stride;
        ri.pad = 0;
        ri.RGBFormat = fmt;
        p96WritePixelArray(&ri, sx, sy, rp, x0, y0, w, h);
        return;
    }
    // CGX names the same two layouts RECTFMT_RGB and RECTFMT_LUT8.
    WritePixelArray(px, sx, sy, stride, rp, x0, y0, w, h,
                    fmt == RGBFB_CLUT ? RECTFMT_LUT8 : RECTFMT_RGB);
}

void rtg_write_rgb(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                   UBYTE *px, SHORT sx, SHORT sy, SHORT stride) {
    write_array(rp, x0, y0, w, h, px, sx, sy, stride, RGBFB_R8G8B8);
}

// One pen per byte. Defined on truecolor screens too, where the RTG library
// resolves each pen through the screen's palette.
void rtg_write_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                    UBYTE *px, SHORT sx, SHORT sy, SHORT stride) {
    write_array(rp, x0, y0, w, h, px, sx, sy, stride, RGBFB_CLUT);
}
