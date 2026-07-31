// SPDX-License-Identifier: 0BSD
//
// How a testcase registers itself with the harness, and nothing else.
//
// Test groups live in their own translation units (tests/drawline.c, ...) and
// define a P96TestGroup that main.c walks. What the scenes draw *with* is
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
    // The mirror image: the scene needs a truecolor screen. An R8G8B8 source
    // array is the case -- on a CLUT screen the RTG library would have to
    // solve for the nearest pen of every pixel, which is not what any of these
    // calls promise. PixelArray-lut8 covers the palette side instead.
    bool truecolor_only;
};

struct P96TestGroup {
    const char *name;
    const struct P96Test *tests;
    int count;
};

// The groups the harness walks. Each is defined by its own file in tests/.
extern const struct P96TestGroup DrawLineGroup;
extern const struct P96TestGroup RectFillGroup;
extern const struct P96TestGroup ClipBlitGroup;
extern const struct P96TestGroup BltTemplateGroup;
extern const struct P96TestGroup BltPatternGroup;
extern const struct P96TestGroup BltBitMapGroup;
extern const struct P96TestGroup ScrollRasterGroup;
extern const struct P96TestGroup PixelArrayGroup;

#endif
