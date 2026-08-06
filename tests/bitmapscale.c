// SPDX-License-Identifier: 0BSD
//
// BitMapScale() testcases.
//
// The source picture is the project logo, committed as
// logo/p96cts-logo-320x200x8.png: the 24-bit logo Floyd-Steinberg-dithered
// against the palette.c pen table (ImageMagick -remap, then reindexed so
// every pixel value is its pen number). Dither texture and hard edges at
// every frequency mean a scaler that picks even one wrong source row or
// column shows it. The pens resolve through the same palette on a truecolor
// screen, so both depths draw the same picture.
//
// Each scale also cross-checks the autodoc invariant that the output size
// BitMapScale reports equals what ScalerDiv computes: a disagreement is
// drawn as a red square at the destination origin. P96's truecolor scaler
// fails this on every downscale (one extra pixel in each direction), so the
// 24-bit goldens record red flags until iComp fixes it.

#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/scale.h>
#include <string.h>

#include "p96cts.h"
#include "gfx.h"
#include "palette.h"
#include "pngio.h"
#include "rtg.h"

#define LOGO_PATH "logo/p96cts-logo-320x200x8.png"
#define LOGO_W 320
#define LOGO_H 200

// The logo in a bitmap of the scene's own pixel format. Pens go in as pens on
// a palette screen and as their palette colors on a truecolor one.
static struct BitMap *logo_source(struct RastPort *rp) {
    SHORT w, h;
    UBYTE *pens = read_png(LOGO_PATH, &w, &h, 1);

    if (!pens)
        return NULL;
    if (w != LOGO_W || h != LOGO_H) {
        FreeVec(pens);
        return NULL;
    }

    struct BitMap *bm = rtg_alloc_friend(rp->BitMap, w, h);
    if (!bm) {
        FreeVec(pens);
        return NULL;
    }

    struct RastPort srp = *rp;
    srp.Layer = NULL;
    srp.BitMap = bm;

    if (gfx_truecolor) {
        UBYTE *rgb = AllocVec((ULONG)w * h * 3, MEMF_ANY);

        if (!rgb) {
            rtg_free_friend(bm);
            FreeVec(pens);
            return NULL;
        }
        for (LONG i = 0; i < (LONG)w * h; i++) {
            ULONG c = pen_rgb(pens[i]);

            rgb[i * 3] = (UBYTE)(c >> 16);
            rgb[i * 3 + 1] = (UBYTE)(c >> 8);
            rgb[i * 3 + 2] = (UBYTE)c;
        }
        rtg_write_rgb(&srp, 0, 0, w, h, rgb, 0, 0, (SHORT)(w * 3));
        FreeVec(rgb);
    } else {
        rtg_write_pens(&srp, 0, 0, w, h, pens, 0, 0, w);
    }
    FreeVec(pens);
    return bm;
}

static void scale_into(struct BitMap *src, SHORT sx, SHORT sy, SHORT sw,
                       SHORT sh, struct RastPort *rp, SHORT dx, SHORT dy,
                       UWORD xsf, UWORD xdf, UWORD ysf, UWORD ydf) {
    struct BitScaleArgs bsa;

    memset(&bsa, 0, sizeof bsa);
    bsa.bsa_SrcX = (UWORD)sx;
    bsa.bsa_SrcY = (UWORD)sy;
    bsa.bsa_SrcWidth = (UWORD)sw;
    bsa.bsa_SrcHeight = (UWORD)sh;
    bsa.bsa_XSrcFactor = xsf;
    bsa.bsa_XDestFactor = xdf;
    bsa.bsa_YSrcFactor = ysf;
    bsa.bsa_YDestFactor = ydf;
    bsa.bsa_DestX = (UWORD)dx;
    bsa.bsa_DestY = (UWORD)dy;
    bsa.bsa_SrcBitMap = src;
    bsa.bsa_DestBitMap = rp->BitMap;
    BitMapScale(&bsa);

    if (bsa.bsa_DestWidth != ScalerDiv((UWORD)sw, xdf, xsf) ||
        bsa.bsa_DestHeight != ScalerDiv((UWORD)sh, ydf, ysf))
        gfx_fill(rp, dx, dy, dx + 7, dy + 7, gfx_pen(2));
}

// The whole logo shrunk by five ratios, so every reduction has to merge
// detail rather than just drop rows.
static void t_down(struct RastPort *rp, SHORT w, SHORT h) {
    gfx_clear(rp, w, h, 0);
    if (w < 320 || h < 200)
        return;

    struct BitMap *src = logo_source(rp);
    if (!src)
        return;

    scale_into(src, 0, 0, LOGO_W, LOGO_H, rp, 0, 0, 2, 1, 2, 1);
    scale_into(src, 0, 0, LOGO_W, LOGO_H, rp, 166, 2, 3, 1, 3, 1);
    scale_into(src, 0, 0, LOGO_W, LOGO_H, rp, 0, 108, 5, 2, 5, 2);
    scale_into(src, 0, 0, LOGO_W, LOGO_H, rp, 136, 110, 4, 1, 4, 1);
    scale_into(src, 0, 0, LOGO_W, LOGO_H, rp, 224, 112, 5, 1, 5, 1);

    WaitBlit();
    rtg_free_friend(src);
}

// A small crop blown up by four ratios, two of them fractional: expansion
// exposes the DDA's pixel replication choices directly.
static void t_up(struct RastPort *rp, SHORT w, SHORT h) {
    const SHORT cx = 138, cy = 54, cw = 60, ch = 36;

    gfx_clear(rp, w, h, 0);
    if (w < 320 || h < 200)
        return;

    struct BitMap *src = logo_source(rp);
    if (!src)
        return;

    scale_into(src, cx, cy, cw, ch, rp, 0, 4, 1, 2, 1, 2);
    scale_into(src, cx, cy, cw, ch, rp, 128, 4, 2, 5, 2, 5);
    scale_into(src, cx, cy, cw, ch, rp, 0, 88, 1, 3, 1, 3);
    scale_into(src, cx, cy, cw, ch, rp, 180, 102, 3, 7, 3, 7);

    WaitBlit();
    rtg_free_friend(src);
}

// One ratio spelled four ways: the autodoc says the factors are "equivalent
// to the ratio but not necessarily the same numbers", so 1:2, 100:200, 3:6
// and 5:10 all halve the logo. Whether they place identical pixels is the
// reference's call; the golden records it.
//
// The source region is 318x198 rather than the whole logo: P96's truecolor
// scaler makes every downscale one pixel wider and taller than ScalerDiv
// says (the red flags record the disagreement), so a 159-wide result
// really comes out 160 wide and four tiles fill the scene exactly. Sized
// from the full logo, that extra column would land outside the scene.
static void t_ratios(struct RastPort *rp, SHORT w, SHORT h) {
    gfx_clear(rp, w, h, 0);
    if (w < 320 || h < 200)
        return;

    struct BitMap *src = logo_source(rp);
    if (!src)
        return;

    scale_into(src, 0, 0, 318, 198, rp, 0, 0, 2, 1, 2, 1);
    scale_into(src, 0, 0, 318, 198, rp, 160, 0, 200, 100, 200, 100);
    scale_into(src, 0, 0, 318, 198, rp, 0, 100, 6, 3, 6, 3);
    scale_into(src, 0, 0, 318, 198, rp, 160, 100, 10, 5, 10, 5);

    WaitBlit();
    rtg_free_friend(src);
}

static const struct P96Test TESTS[] = {
    {.name = "down", .fn = t_down},
    {.name = "up", .fn = t_up},
    {.name = "ratios", .fn = t_ratios},
};

const struct P96TestGroup BitMapScaleGroup = {
    "BitMapScale", TESTS, (int)(sizeof TESTS / sizeof TESTS[0])
};
