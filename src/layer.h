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

struct RastPort *layer_install(struct BitMap *bm);
bool layer_clip(struct BitMap *bm, SHORT w, SHORT h);
bool layer_point_obscured(SHORT x, SHORT y);
bool layer_clip_broken(const char *name, ULONG micros);
void layer_free(void);

#endif
