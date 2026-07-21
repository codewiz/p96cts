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

/* Fill the whole scene with one color, in JAM1: a pen number on a palette
 * screen, 0x00RRGGBB on truecolor. 0 is black either way. */
void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color);

/* JAM1-fill a rectangle in the given color (pen or 0x00RRGGBB as above).
 * On a palette screen the corners go to RectFill as given, including
 * deliberately swapped ones; the truecolor path sorts them first, since
 * p96RectFill's contract requires min <= max.
 *
 * Deliberately not inline: the body is two library calls, which dwarf the
 * call overhead, and inlining it would drag proto/graphics.h and
 * proto/Picasso96.h into every file that includes this header. */
void p96cts_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color);

/* --- modes and readback --------------------------------------------------- */

/* No display id matched. graphics/modeid.h defines INVALID_ID as ~0, an int,
 * so comparing it against a ULONG display id is a signedness mismatch. */
#define P96CTS_INVALID_MODE ((ULONG)~0UL)

/* Find a display id of the given size/depth, or P96CTS_INVALID_MODE. `monitor`
 * selects by mode-name prefix ("PAL", "Z3660", ...); NULL matches any. When
 * name_out is given, the matched mode's name is copied into it. */
ULONG p96cts_find_mode(int w, int h, int depth, const char *monitor,
                       char *name_out, int name_len);

/* Dump the display database to stdout so a usable mode can be picked. */
void p96cts_list_modes(void);

/* Name an RGBFormat value, for reporting what a run actually rendered on. */
const char *p96cts_format_name(ULONG fmt);

/* Read the scene back into a freshly AllocVec'd buffer, or NULL. The caller
 * FreeVec's it. Pens gives one byte per pixel and needs a palette bitmap; rgb
 * gives three, converted from whatever the screen's own format is. */
UBYTE *p96cts_read_pens(struct RastPort *rp, SHORT w, SHORT h, int depth);
UBYTE *p96cts_read_rgb(struct RastPort *rp, SHORT w, SHORT h);

#endif
