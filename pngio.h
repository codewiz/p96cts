/* SPDX-License-Identifier: 0BSD */

/* PNG images (pngio.c), used by the harness to capture and compare scenes.
 * bpp is 1 for an indexed image of pen values or 3 for RGB.
 *
 * Named pngio rather than png because -I. precedes the libpng include path,
 * so a png.h here would shadow libpng's own for every file that includes it.
 */

#ifndef P96CTS_PNGIO_H
#define P96CTS_PNGIO_H

#include <exec/types.h>

/* Returns 0 on success. */
int p96cts_write_png(const char *path, const UBYTE *px, SHORT w, SHORT h,
                     int bpp);

/* Returns an AllocVec'd w*h*bpp buffer for the caller to FreeVec, or NULL.
 * An image of the other kind is refused, not converted, since a converted
 * reference is no longer a reference. */
UBYTE *p96cts_read_png(const char *path, SHORT *w, SHORT *h, int bpp);

#endif
