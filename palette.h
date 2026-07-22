/* SPDX-License-Identifier: 0BSD */

/* The colors pen numbers stand for. One table serves both the screen the run
 * opens and the palette written into a PNG, so an image on disk looks like
 * what was rendered rather than being a second interpretation of the same pen
 * numbers.
 */

#ifndef P96CTS_PALETTE_H
#define P96CTS_PALETTE_H

#include <exec/types.h>

/* Every pen a depth-8 run can name. */
#define P96CTS_PALETTE_ENTRIES 256

const ULONG *p96cts_palette(void);

#endif
