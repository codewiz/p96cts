/* SPDX-License-Identifier: 0BSD */

/* The shared backdrop scene (backdrop.c). */

#ifndef P96CTS_BACKDROP_H
#define P96CTS_BACKDROP_H

#include <exec/types.h>

struct RastPort;

/* Draw a landscape over the w x h region, textured per pixel so no part of it
 * is flat enough to hide a misplaced pixel. For testcases that need something
 * substantial to draw over or copy around. */
void p96cts_backdrop(struct RastPort *rp, SHORT w, SHORT h);

#endif
