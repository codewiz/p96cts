// SPDX-License-Identifier: 0BSD
//
// How a testcase registers itself with the harness, and nothing else.
//
// Test groups live in their own translation units (tests/drawline.c, ...) and
// define a P96TestGroup that p96cts.c walks. What the scenes draw *with* is
// in gfx.h and backdrop.h.

#ifndef P96CTS_H
#define P96CTS_H

#include <exec/types.h>
#include <stdbool.h>

struct RastPort;

// One testcase: render a complete scene into rp, which is w x h pixels.
// A testcase must clear the whole scene itself, since the same RastPort is
// reused across testcases.
struct P96Test {
    const char *name;
    void (*fn)(struct RastPort *rp, SHORT w, SHORT h);
    // The scene tests something a truecolor screen has no equivalent of, so it
    // runs on palette screens only. Pens are not a reason -- every pen has a
    // defined color on both kinds of screen; rp->Mask, which selects
    // bitplanes, is.
    bool palette_only;
};

struct P96TestGroup {
    const char *name;
    const struct P96Test *tests;
    int count;
};

// The groups the harness walks. Each is defined by its own file in tests/.
extern const struct P96TestGroup DrawLineGroup;
extern const struct P96TestGroup FillRectGroup;
extern const struct P96TestGroup CopyRectGroup;

#endif
