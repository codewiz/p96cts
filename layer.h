// SPDX-License-Identifier: 0BSD
//
// An optional Layer over the render bitmap, so scenes exercise the driver's
// clipped path instead of the unclipped one. See layer.c.

#ifndef P96CTS_LAYER_H
#define P96CTS_LAYER_H

#include <exec/types.h>
#include <stdbool.h>

struct BitMap;
struct RastPort;

// Open layers.library and lay a single simple-refresh layer over the whole of
// bm. Returns the layer's RastPort to draw through, or NULL, having said why.
struct RastPort *layer_install(struct BitMap *bm);

// Tear it back down. A no-op if layer_install() was never called or failed,
// so it can sit unconditionally on the cleanup path.
void layer_free(void);

#endif
