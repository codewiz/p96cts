// SPDX-License-Identifier: 0BSD
//
// Run one testcase: render its scene and capture or judge the result.
// See runtest.c.

#ifndef P96CTS_RUNTEST_H
#define P96CTS_RUNTEST_H

#include <exec/types.h>
#include <stdbool.h>

struct P96Test;
struct RastPort;

// Everything running one testcase needs to know, and nothing more: main.c
// fills one per mode from its own argument bookkeeping.
struct TestOpts {
    SHORT w, h;             // scene: the region rendered and compared
    SHORT screen_w, screen_h;
    int depth;
    int bpp;        // bytes per compared pixel: 1 (pen) or 3 (R8G8B8)
    bool capture;   // write the golden instead of comparing against it
    bool clip;      // obscuring clip layers are up; check them after the scene
    const char *dir;        // where this run's own images go
    const char *golden_dir; // where the references it compares against live
};

bool run_test(const struct P96Test *t, const char *name, struct RastPort *rp,
              const struct TestOpts *o);

#endif
