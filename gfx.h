/* SPDX-License-Identifier: 0BSD */

/* Everything gfx.c offers: the color helpers scenes draw through, and the
 * display-database lookup and scene readback the harness drives.
 */

#ifndef P96CTS_GFX_H
#define P96CTS_GFX_H

#include <exec/types.h>

struct RastPort;

/* --- colors --------------------------------------------------------------- */

/* Whether the current run is on a truecolor mode (depth > 8). Testcases that
 * run on both kinds of screen branch on this to pick their color calls:
 * pens on a palette screen, direct RGB on truecolor. */
extern int p96cts_truecolor;

/* The color for the current run: a pen number on a palette screen, direct
 * 0x00RRGGBB on truecolor. Scenes pass both and let the run pick. Inline
 * because scenes call it once per drawing primitive, sometimes in a loop, and
 * it compiles to a load and a select. */
static inline ULONG p96cts_color(ULONG pen, ULONG rgb) {
    return p96cts_truecolor ? rgb : pen;
}

void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color);
void p96cts_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color);

/* --- modes and readback --------------------------------------------------- */

/* No display id matched. graphics/modeid.h defines INVALID_ID as ~0, an int,
 * so comparing it against a ULONG display id is a signedness mismatch. */
#define P96CTS_INVALID_MODE ((ULONG)~0UL)

ULONG p96cts_find_mode(int w, int h, int depth, const char *monitor,
                       char *name_out, int name_len);
void p96cts_list_modes(void);
const char *p96cts_format_name(ULONG fmt);
UBYTE *p96cts_read_pens(struct RastPort *rp, SHORT w, SHORT h, int depth);
UBYTE *p96cts_read_rgb(struct RastPort *rp, SHORT w, SHORT h);

#endif
