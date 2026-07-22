// SPDX-License-Identifier: 0BSD
//
// The shared backdrop scene (backdrop.c).

#ifndef P96CTS_BACKDROP_H
#define P96CTS_BACKDROP_H

#include <exec/types.h>

struct RastPort;

void p96cts_backdrop(struct RastPort *rp, SHORT w, SHORT h);

#endif
