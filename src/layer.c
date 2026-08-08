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

#include "layer.h"
#include "report.h"

struct Library *LayersBase;

static struct Layer_Info *layer_info;
static struct Layer *layer;

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
    layer = CreateUpfrontLayer(layer_info, bm, 0, 0, w - 1, h - 1, LAYERSIMPLE,
                               NULL);
    if (!layer) {
        rpt_errorf("CreateUpfrontLayer %ldx%ld failed", (long)w, (long)h);
        return NULL;
    }
    return layer->rp;
}

// Tear it back down. A no-op if layer_install() was never called or failed,
// so it can sit unconditionally on the cleanup path.
void layer_free(void) {
    if (layer)
        DeleteLayer(0, layer);
    layer = NULL;
    if (layer_info)
        DisposeLayerInfo(layer_info);
    layer_info = NULL;
    if (LayersBase)
        CloseLibrary(LayersBase);
    LayersBase = NULL;
}
