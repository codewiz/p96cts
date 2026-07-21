/* SPDX-License-Identifier: 0BSD */

/* How a testcase registers itself with the harness, and nothing else.
 *
 * Test groups live in their own translation units (tests/drawline.c, ...) and
 * define a P96TestGroup that p96cts.c walks. What the scenes draw *with* is
 * in gfx.h and backdrop.h.
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
    /* The group's scenes pick colors with SetAPen/SetBPen, which sets a pen
     * number: meaningful on a palette screen, undefined on a truecolor one.
     * Such a group only runs at depth 8 until it learns p96EncodeColor. */
    int clut_only;
};

/* The groups the harness walks. Each is defined by its own file in tests/. */
extern const struct P96TestGroup DrawLineGroup;
extern const struct P96TestGroup FillRectGroup;
extern const struct P96TestGroup CopyRectGroup;

#endif
