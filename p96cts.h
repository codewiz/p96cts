/* SPDX-License-Identifier: 0BSD */

/* p96cts -- P96 driver conformance test suite.
 *
 * Test groups live in their own translation units (drawline.c, ...) and
 * register themselves through a P96TestGroup that p96cts.c walks.
 */

#ifndef P96CTS_H
#define P96CTS_H

#include <exec/types.h>

struct RastPort;

/* One testcase: render a complete scene into rp, which is w x h pixels.
 * A testcase must clear the whole scene itself, since the same RastPort is
 * reused across testcases. */
struct P96Test {
    const char *name;
    void (*fn)(struct RastPort *rp, SHORT w, SHORT h);
    int clut_only; /* as for the group flag, but for one scene */
};

struct P96TestGroup {
    const char *name;
    const struct P96Test *tests;
    int count;
    /* The group's scenes pick colours with SetAPen/SetBPen, which sets a pen
     * number: meaningful on a palette screen, undefined on a truecolor one.
     * Such a group only runs at depth 8 until it learns p96EncodeColor. */
    int clut_only;
};

/* Whether the current run is on a truecolor mode (depth > 8). Testcases that
 * run on both kinds of screen branch on this to pick their colour calls:
 * pens on a palette screen, direct RGB on truecolor. */
extern int p96cts_truecolor;

/* Fill the whole scene with one colour, in JAM1: a pen number on a palette
 * screen, 0x00RRGGBB on truecolor. 0 is black either way. */
void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG colour);

/* JAM1-fill a rectangle in the given colour (pen or 0x00RRGGBB as above).
 * On a palette screen the corners go to RectFill as given, including
 * deliberately swapped ones; the truecolor path sorts them first, since
 * p96RectFill's contract requires min <= max. */
void p96cts_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG colour);

/* The colour for the current run: a pen number on a palette screen, direct
 * 0x00RRGGBB on truecolor. Scenes pass both and let the run pick. */
ULONG p96cts_colour(ULONG pen, ULONG rgb);

/* Draw the shared backdrop scene (backdrop.c): a landscape, textured per
 * pixel so no region of it is flat enough to hide a misplaced pixel. For
 * testcases that need something substantial to draw over or copy around. */
void p96cts_backdrop(struct RastPort *rp, SHORT w, SHORT h);

/* PNG images (png.c). bpp is 1 for an indexed image of pen values or 3 for
 * RGB. Write returns 0 on success; read returns an AllocVec'd w*h*bpp
 * buffer, or NULL -- an image of the other kind is refused, not converted,
 * since a converted reference is no longer a reference. */
int p96cts_write_png(const char *path, const UBYTE *px, SHORT w, SHORT h,
                     int bpp);
UBYTE *p96cts_read_png(const char *path, SHORT *w, SHORT *h, int bpp);

extern const struct P96TestGroup DrawLineGroup;
extern const struct P96TestGroup FillRectGroup;
extern const struct P96TestGroup CopyRectGroup;

#endif
