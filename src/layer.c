// SPDX-License-Identifier: 0BSD
//
// An optional Layer over the render bitmap.
//
// Every scene so far has been drawn through a RastPort with no Layer, which is
// the unclipped path: graphics.library hands the rectangle straight to the
// driver. Real applications draw into windows, so the calls arrive split
// against a ClipRect list instead, and a driver can get that path wrong on its
// own -- wrong offsets per fragment, a fragment dropped, a pattern phase reset
// at every fragment boundary rather than carried across the whole operation.
//
// The layer here covers the entire area, so nothing is actually trimmed and
// the ClipRect list is a single rectangle. That is the point: the pixels must
// come out identical to the unclipped run, so the existing golden set is the
// reference and any difference is the clipped path diverging from the plain
// one.
//
// layer_clip() then makes the clipping real: two thin upfront layers crossing
// the scene, so the drawing layer's ClipRect list has genuine holes and the
// calls arrive at the driver in several fragments. The ordinary goldens stay
// the reference: the harness asks layer_point_obscured() which pixels the
// clip layers cover and excludes them from the comparison, since what a
// readback returns under an obscuring layer is nobody's defined output.
//
// Simple refresh, because there is nothing to obscure this layer and so
// nothing for smart refresh to save; and CreateUpfrontLayer rather than the
// Hook form, so the layer gets layers' default backfill -- the one that clears
// to pen 0, which is what EraseRect() already does on an unlayered RastPort.
// LAYERS_NOBACKFILL would change what ScrollRasterBF() leaves behind.

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <graphics/clip.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/rastport.h>
#include <string.h>

#include "countof.h"
#include "gfx.h"
#include "layer.h"
#include "report.h"
#include "rtg.h"

struct Library *LayersBase;

// The layers themselves are not tracked: layers.library keeps them on
// layer_info's own front-to-back list, which is what layer_free() walks.
static struct Layer_Info *layer_info;

// What each clip layer held right after its fill, in front-to-back list
// order, and the depth to read it back at. A snapshot rather than a
// recomputation because the fill comes back quantized by the screen's own
// pixel format. layer_clip_broken() compares against these after every
// testcase: a scene that draws through the clipping into a layer is caught
// there, charged to the test that did it, and the fill restored so the
// damage does not follow into the tests after it.
#define MAX_CLIP_LAYERS 2
static UBYTE *clip_snap[MAX_CLIP_LAYERS];
static SHORT clip_depth;

// Open layers.library and lay a single simple-refresh layer over the whole of
// bm. Returns the layer's RastPort to draw through, or NULL, having said why.
struct RastPort *layer_install(struct BitMap *bm) {
    // The bitmap's own size, not the screen's or the scene's: the reference
    // bitmap is deliberately far wider than the mode, and a layer narrower
    // than the bitmap would clip the walls rather than let a stray write
    // break them.
    const LONG w = GetBitMapAttr(bm, BMA_WIDTH);
    const LONG h = GetBitMapAttr(bm, BMA_HEIGHT);

    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    if (!LayersBase) {
        rpt_errorf("failed to open layers.library 39");
        return NULL;
    }
    layer_info = NewLayerInfo();
    if (!layer_info) {
        rpt_errorf("NewLayerInfo failed");
        return NULL;
    }
    // Corners are inclusive, and the layer sits at the bitmap's origin so
    // layer-relative coordinates are the bitmap's own: a scene needs no idea
    // that it is being clipped at all.
    struct Layer *l = CreateUpfrontLayer(layer_info, bm, 0, 0, w - 1, h - 1,
                                         LAYERSIMPLE, NULL);
    if (!l) {
        rpt_errorf("CreateUpfrontLayer %ldx%ld failed", (long)w, (long)h);
        return NULL;
    }
    return l->rp;
}

// Two thin layers over the scene, one tall and one wide, crossing off-center:
// an asymmetric cross. Created upfront, they obscure the drawing layer, so
// its ClipRect list has genuine holes -- a full-scene Draw() arrives in
// three pieces -- while hiding little of the image. Neither layer reaches the
// scene's edges, and by different margins at each end: a span crossing the
// layer's line fragments only where the layer actually is, so where an
// operation splits depends on both coordinates, and the ClipRect list gains
// corner rectangles around the layer tips that full-span bars never produce.
// The vertical one straddles a 16-pixel word boundary so the fragments on
// either side of it are unaligned. Both are filled once with pen 5 (dim
// gray), a color no scene builds on, so what they cover is recognizable in
// the failure images; their pixels are excluded from the comparison either
// way.
//
// Both layers are a multiple of 16 pixels wide because the snapshot below
// reads them back with gfx_read_pens(), whose ReadPixelArray8 pads rows to
// 16-pixel granules and would write past a narrower buffer. Their edges
// still land mid-word, which is what matters for the fragments.
//
// Requires layer_install() to have succeeded, so these sit in front of the
// drawing layer. Returns false, having said why, if one cannot be created.
bool layer_clip(struct BitMap *bm, SHORT w, SHORT h) {
    const SHORT vx = (SHORT)(w * 7 / 16 - 4), vw = 16;
    const SHORT vy0 = (SHORT)(h / 16), vy1 = (SHORT)(h - h / 9 - 1);
    const SHORT hy = (SHORT)(h * 2 / 7), hh = 7;
    const SHORT hx0 = (SHORT)(((w / 10) & ~15) + 8);
    const SHORT hx1 = (SHORT)(hx0 + ((w - hx0 - w / 20) & ~15) - 1);
    const struct {
        SHORT x0, y0, x1, y1;
    } r[] = {
        {vx, vy0, (SHORT)(vx + vw - 1), vy1},
        {hx0, hy, hx1, (SHORT)(hy + hh - 1)},
    };

    for (int i = 0; i < (int)countof(r); i++) {
        struct Layer *l = CreateUpfrontLayer(layer_info, bm, r[i].x0, r[i].y0,
                                             r[i].x1, r[i].y1, LAYERSIMPLE,
                                             NULL);
        if (!l) {
            rpt_errorf("CreateUpfrontLayer for clip layer %d failed", i);
            return false;
        }
        gfx_fill(l->rp, 0, 0, (SHORT)(r[i].x1 - r[i].x0),
                 (SHORT)(r[i].y1 - r[i].y0), gfx_pen(5));
    }

    // Snapshot what the fills produced, through each layer's own RastPort.
    // Where the layers cross, the back one is itself obscured and its bytes
    // come back undefined; obscured_for() keeps those out of the comparison.
    WaitBlit();
    clip_depth = (SHORT)GetBitMapAttr(bm, BMA_DEPTH);
    int i = 0;
    for (struct Layer *l = layer_info->top_layer;
         l && l->back && i < MAX_CLIP_LAYERS; l = l->back, i++) {
        const SHORT lw = (SHORT)(l->bounds.MaxX - l->bounds.MinX + 1);
        const SHORT lh = (SHORT)(l->bounds.MaxY - l->bounds.MinY + 1);

        clip_snap[i] = gfx_truecolor
                           ? rtg_read_rgb(l->rp, 0, 0, lw, lh)
                           : gfx_read_pens(l->rp, 0, 0, lw, lh, clip_depth);
        if (!clip_snap[i]) {
            rpt_errorf("snapshotting clip layer %d failed", i);
            return false;
        }
    }
    return true;
}

// Whether bitmap point (x, y) belongs to a clip layer in front of l. The two
// clip layers cross, and the intersection is the front one's: reading the back
// one there goes through an obscured ClipRect, which a simple-refresh layer
// has no content for, so those bytes are undefined and must not be compared.
static bool obscured_for(const struct Layer *l, SHORT x, SHORT y) {
    for (struct Layer *f = layer_info->top_layer; f && f != l; f = f->back)
        if (x >= f->bounds.MinX && x <= f->bounds.MaxX &&
            y >= f->bounds.MinY && y <= f->bounds.MaxY)
            return true;
    return false;
}

// After a testcase: has anything leaked through the clipping into the layers
// themselves? A hit is reported as the testcase's failure, and the fill is
// restored so it does not follow into the tests after it. False when clipping
// is not installed, so the call can sit unconditionally in the run loop.
bool layer_clip_broken(const char *name, ULONG micros) {
    bool broken = false;
    int i = 0;

    if (!layer_info)
        return false;
    for (struct Layer *l = layer_info->top_layer;
         l && l->back && i < MAX_CLIP_LAYERS && clip_snap[i]; l = l->back, i++) {
        const SHORT lw = (SHORT)(l->bounds.MaxX - l->bounds.MinX + 1);
        const SHORT lh = (SHORT)(l->bounds.MaxY - l->bounds.MinY + 1);
        const int bpp = gfx_truecolor ? 3 : 1;
        UBYTE *now = gfx_truecolor
                         ? rtg_read_rgb(l->rp, 0, 0, lw, lh)
                         : gfx_read_pens(l->rp, 0, 0, lw, lh, clip_depth);
        bool hit = false;

        if (!now)
            continue;
        for (SHORT y = 0; y < lh && !hit; y++)
            for (SHORT x = 0; x < lw && !hit; x++) {
                ULONG p = ((ULONG)y * lw + x) * bpp;

                hit = !obscured_for(l, (SHORT)(l->bounds.MinX + x),
                                    (SHORT)(l->bounds.MinY + y)) &&
                      memcmp(now + p, clip_snap[i] + p, bpp);
            }
        if (hit) {
            rpt_failure(name, micros,
                        "drew through the clipping into the layer at %d,%d",
                        l->bounds.MinX, l->bounds.MinY);
            gfx_fill(l->rp, 0, 0, (SHORT)(lw - 1), (SHORT)(lh - 1),
                     gfx_pen(5));
            broken = true;
        }
        FreeVec(now);
    }
    return broken;
}

// Whether scene point (x, y) lies under one of the obscuring clip layers --
// every layer above the bottom-most, which is the drawing layer, straight
// off layers.library's own front-to-back list. Layer bounds are bitmap
// coordinates, which the drawing layer's origin placement makes the scene's
// own. False whenever no clipping is installed, so callers need not test.
bool layer_point_obscured(SHORT x, SHORT y) {
    if (!layer_info)
        return false;
    for (struct Layer *l = layer_info->top_layer; l && l->back; l = l->back)
        if (x >= l->bounds.MinX && x <= l->bounds.MaxX &&
            y >= l->bounds.MinY && y <= l->bounds.MaxY)
            return true;
    return false;
}

// Tear it back down. A no-op if layer_install() was never called or failed,
// so it can sit unconditionally on the cleanup path.
void layer_free(void) {
    for (int i = 0; i < MAX_CLIP_LAYERS; i++) {
        FreeVec(clip_snap[i]);
        clip_snap[i] = NULL;
    }
    if (layer_info) {
        // Front to back, so the clip layers go before the drawing layer they
        // obscure. This also cleans up after a partial layer_clip().
        while (layer_info->top_layer)
            DeleteLayer(0, layer_info->top_layer);
        DisposeLayerInfo(layer_info);
    }
    layer_info = NULL;
    if (LayersBase)
        CloseLibrary(LayersBase);
    LayersBase = NULL;
}
