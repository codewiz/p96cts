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
};

struct P96TestGroup {
    const char *name;
    const struct P96Test *tests;
    int count;
};

/* Fill the whole scene with one pen, in JAM1. */
void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, int pen);

/* Indexed 8-bit PNG images (png.c). Write returns 0 on success; read
 * returns an AllocVec'd w*h buffer of pen values, or NULL. */
int p96cts_write_png(const char *path, const UBYTE *idx, SHORT w, SHORT h);
UBYTE *p96cts_read_png(const char *path, SHORT *w, SHORT *h);

extern const struct P96TestGroup DrawLineGroup;

#endif
