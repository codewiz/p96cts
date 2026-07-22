// SPDX-License-Identifier: 0BSD
//
// The shared backdrop scene (backdrop.c).

#ifndef P96CTS_BACKDROP_H
#define P96CTS_BACKDROP_H

#include <exec/types.h>

struct RastPort;

void p96cts_backdrop(struct RastPort *rp, SHORT w, SHORT h);

// Release the cached scene. The run loop calls this once at the end; scenes
// never need to.
void p96cts_backdrop_free(void);

#endif
