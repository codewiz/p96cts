// SPDX-License-Identifier: 0BSD
//
// Run one testcase: poison the scene, render it, and either capture the
// result as the golden or compare it against one, reporting PASS or the
// failure with its first differing pixels and images. main.c owns everything
// around this -- arguments, modes, the screen and the loop over testcases --
// and hands in a struct TestOpts with just what one run needs.

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p96cts.h"
#include "gfx.h"
#include "layer.h"
#include "pngio.h"
#include "report.h"
#include "runtest.h"
#include "rtg.h"
#include "timer.h"
#include "wall.h"

#define MAX_REPORTED_DIFFS 8

// "<dir>/<name><suffix>", freshly allocated, or NULL. On the heap rather than
// in a buffer on the stack: a Shell gives a command 4K of stack by default,
// and paths get composed underneath libpng, which wants the rest of it.
static char *image_path(const char *dir, const char *name, const char *suffix) {
    char *path = NULL;

    if (asprintf(&path, "%s/%s%s", dir, name, suffix) < 0) {
        rpt_errorf("out of memory composing a path for %s", name);
        return NULL;
    }
    return path;
}

// CreateDir() makes one level, so walk the path creating each component.
// Components that already exist fail harmlessly with ERROR_OBJECT_EXISTS.
static void make_path(const char *path) {
    char *buf = strdup(path);

    if (!buf)
        return;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] != '/')
            continue;
        buf[i] = 0;
        BPTR lock = CreateDir((STRPTR)buf);
        if (lock)
            UnLock(lock);
        buf[i] = '/';
    }
    BPTR lock = CreateDir((STRPTR)buf);
    if (lock)
        UnLock(lock);
    free(buf);
}

// As image_path(), for a file this run is about to write: the directory is
// created here rather than up front, so a run that writes nothing -- every
// scene passing, which is the common case -- leaves no empty output/<monitor>/
// <mode> tree behind.
static char *out_path(const struct TestOpts *o, const char *name,
                      const char *suffix) {
    char *path = image_path(o->dir, name, suffix);

    if (path)
        make_path(o->dir);
    return path;
}

// The comparison itself: how many pixels differ from the golden. Pixels under
// the obscuring clip layers are excluded -- what a readback returns there is
// not the scene's output -- as they are from the diff listing and diff image.
static ULONG count_diffs(const UBYTE *idx, const UBYTE *gold,
                         const struct TestOpts *o) {
    ULONG bad = 0;

    for (SHORT y = 0; y < o->h; y++)
        for (SHORT x = 0; x < o->w; x++) {
            ULONG p = ((ULONG)y * o->w + x) * o->bpp;

            if (!layer_point_obscured(x, y) &&
                memcmp(idx + p, gold + p, o->bpp))
                bad++;
        }
    return bad;
}

// Keep what a failing scene rendered, and a picture of where it went wrong:
// <test>.fail.png is the render itself, <test>.diff.png marks the differing
// pixels in red over the golden dimmed to gray -- at full intensity the scene
// buries a few single-pixel diffs. Returns true if an image could not be
// written.
static bool write_failure_images(const char *name, const UBYTE *idx,
                                 const UBYTE *gold, const struct TestOpts *o) {
    bool failed = false;
    int bpp = o->bpp;

    char *path = out_path(o, name, ".fail.png");
    if (!path || write_png(path, idx, o->w, o->h, bpp))
        failed = true;
    else
        rptf("       captured %s", path);
    free(path);

    UBYTE *d = (UBYTE *)AllocVec((ULONG)o->w * o->h * bpp, MEMF_CLEAR);
    if (!d) {
        rpt_errorf("WARNING: failed to allocate diff buffer for %s", name);
        return failed;
    }
    for (SHORT y = 0; y < o->h; y++)
        for (SHORT x = 0; x < o->w; x++) {
            ULONG p = ((ULONG)y * o->w + x) * bpp;
            bool same = layer_point_obscured(x, y) ||
                        !memcmp(idx + p, gold + p, bpp);
            if (bpp != 3) {
                d[p] = !same ? 2 : (gold[p] ? 5 : 0);
            } else if (!same) {
                d[p] = 255;
                d[p + 1] = d[p + 2] = 0;
            } else {
                UBYTE gray = (UBYTE)((gold[p] + gold[p + 1] + gold[p + 2]) / 6);
                d[p] = d[p + 1] = d[p + 2] = gray;
            }
        }

    path = out_path(o, name, ".diff.png");
    if (!path || write_png(path, d, o->w, o->h, bpp))
        failed = true;
    else
        rptf("       wrote difference to %s", path);
    free(path);
    FreeVec(d);
    return failed;
}

// Compare a rendered scene against its golden and report the result: PASS, or
// the differing pixel count, the first few differing pixels by coordinate.
// Returns true when the scene fails.
static bool diff_scene(const char *name, ULONG us, const UBYTE *idx,
                       const UBYTE *gold, SHORT gw, SHORT gh,
                       const struct TestOpts *o) {
    int bpp = o->bpp;
    int shown = 0;

    if (gw != o->w || gh != o->h) {
        rpt_failure(name, us, "golden is %dx%d, scene is %dx%d", gw, gh, o->w,
                    o->h);
        return true;
    }

    ULONG bad = count_diffs(idx, gold, o);
    if (!bad) {
        rpt_success(name, us);
        return false;
    }

    rpt_failure(name, us, "%lu of %lu pixels differ", (unsigned long)bad,
                (unsigned long)((ULONG)o->w * o->h));

    for (SHORT y = 0; y < o->h && shown < MAX_REPORTED_DIFFS; y++)
        for (SHORT x = 0; x < o->w && shown < MAX_REPORTED_DIFFS; x++) {
            ULONG p = ((ULONG)y * o->w + x) * bpp;
            if (layer_point_obscured(x, y) || !memcmp(idx + p, gold + p, bpp))
                continue;
            if (bpp == 3)
                rptf("       at %3d,%3d golden %02X%02X%02X, got %02X%02X%02X",
                     x, y, gold[p], gold[p + 1], gold[p + 2], idx[p],
                     idx[p + 1], idx[p + 2]);
            else
                rptf("       at %3d,%3d golden %3d, got %3d", x, y, gold[p],
                     idx[p]);
            shown++;
        }
    if (bad > (ULONG)shown)
        rptf("       ... and %lu more", (unsigned long)(bad - shown));
    write_failure_images(name, idx, gold, o);
    return true;
}

// Prepare the shared RastPort for a testcase: lay a loud checkerboard into the
// scene, then reset the render state to a known default.
//
// The poison catches a test that fails to paint every pixel it compares -- it
// shows the leftover and fails against its golden, instead of passing on
// whatever the previous test left there. The state reset (draw mode, pens,
// write mask, line pattern) keeps a test from inheriting anything from whatever
// ran before it, so results cannot depend on the order tests run in.
static void reset_scene(struct RastPort *rp, const struct TestOpts *o) {
    const SHORT cell = 16;
    ULONG color[2] = {
        gfx_color(0xAA, 0xFF00FFUL),
        gfx_color(0x55, 0xFFFF00UL),
    };
    int row = 0;

    rp->Mask = 0xFF; // so the fill reaches every plane
    for (SHORT y = 0; y < o->h; y += cell) {
        SHORT y2 = y + cell - 1 < o->h ? y + cell - 1 : o->h - 1;
        int i = row;

        for (SHORT x = 0; x < o->w; x += cell) {
            SHORT x2 = x + cell - 1 < o->w ? x + cell - 1 : o->w - 1;

            gfx_fill(rp, x, y, x2, y2, color[i]);
            i ^= 1;
        }
        row ^= 1;
    }

    SetABPenDrMd(rp, 1, 0, JAM1);
    SetDrPt(rp, 0xFFFF);
    rp->Mask = 0xFF;
}

// Render one testcase and capture or compare it. Returns true on failure.
//
// `name` is the testcase's full "<group>-<test>" name: the group qualifies it,
// so two groups can both have an "edges" scene and their images cannot
// collide in golden/.
bool run_test(const struct P96Test *t, const char *name, struct RastPort *rp,
              const struct TestOpts *o) {
    bool failed = false;
    int bpp = o->bpp;
    SHORT gw, gh;
    struct EClockVal t0, t1;
    struct Wall wall = {o->w, o->h, o->screen_w, o->screen_h, o->bpp,
                        o->depth, {{0}}};

    // Before reset_scene, so the testcase inherits its render state reset.
    build_walls(rp, &wall);
    reset_scene(rp, o);

    // The timed span is the scene render alone, blitter completion included;
    // walls, readback and comparison are harness overhead and stay outside.
    timer_now(&t0);

    t->fn(rp, o->w, o->h);
    // Wait for the blitter before reading the scene back.
    WaitBlit();

    timer_now(&t1);
    const ULONG us = eclock_micros(&t0, &t1);

    if (wall_broken(rp, &wall, name, us))
        return true;
    if (o->clip && layer_clip_broken(name, us))
        return true;
    UBYTE *idx = bpp == 3 ? rtg_read_rgb(rp, 0, 0, o->w, o->h)
                          : gfx_read_pens(rp, 0, 0, o->w, o->h, o->depth);
    if (!idx) {
        rpt_failure(name, us, "memory allocation failed");
        return true;
    }
    if (o->capture) {
        char *path = out_path(o, name, ".png");
        if (!path || write_png(path, idx, o->w, o->h, bpp))
            failed = true;
        else
            rptf("captured %s (%lu.%03lums)", path,
                 (unsigned long)(us / 1000), (unsigned long)(us % 1000));
        free(path);
        FreeVec(idx);
        return failed;
    }

    UBYTE *gold = NULL;
    {
        char *path = image_path(o->golden_dir, name, ".png");
        if (path)
            gold = read_png(path, &gw, &gh, bpp);
        if (!gold) {
            rpt_failure(name, us, "no golden at %s", path ? path : "?");
            free(path);
            FreeVec(idx);
            return true;
        }
        free(path);
    }

    failed = diff_scene(name, us, idx, gold, gw, gh, o);
    FreeVec(gold);
    FreeVec(idx);
    return failed;
}
