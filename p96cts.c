// SPDX-License-Identifier: 0BSD
//
// p96cts -- P96 driver conformance test suite.
//
// This file is the harness: arguments, the run loop, and the comparison.
// Scenes live in tests/, the graphics.library and P96 calls in gfx.c.

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <intuition/screens.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp: POSIX rather than ISO C, but newlib has it

#include "p96cts.h"
#include "backdrop.h"
#include "gfx.h"
#include "palette.h"
#include "pngio.h"

// Standard AmigaOS version tag, readable with the Version command.
static const char VERSTAG[] = "$VER: p96cts 0.6 (23.7.2026) by Bernie Innocenti";
#define VERSION_LINE (VERSTAG + 6)

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static const struct P96TestGroup *const GROUPS[] = {
    &DrawLineGroup,
    &RectFillGroup,
    &ClipBlitGroup,
    &BltTemplateGroup,
};
#define NGROUPS ((int)(sizeof GROUPS / sizeof GROUPS[0]))

// "<dir>/<name><suffix>", freshly allocated, or NULL. On the heap rather than
// in a buffer on the stack: a Shell gives a command 4K of stack by default,
// and paths get composed underneath libpng, which wants the rest of it.
static char *image_path(const char *dir, const char *name, const char *suffix) {
    char *path = NULL;

    if (asprintf(&path, "%s/%s%s", dir, name, suffix) < 0) {
        printf("out of memory composing a path for %s\n", name);
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

// --- run --------------------------------------------------------------------

// Case-insensitively, so a testcase can be typed as it reads rather than as
// the API capitalizes it: "drawline-solid" finds "DrawLine-solid".
static bool selected(const char *want, const char *name) {
    return !want || !strcasecmp(want, name); // no TEST given: run all
}

// Compose a testcase's full name. Testcases are named for what they do
// ("solid", "overlap-down") and the group supplies the subject, so the name
// a user types and the name on disk are both "<group>-<test>".
static void test_name(char *buf, size_t len, const struct P96TestGroup *grp,
                      const struct P96Test *t) {
    snprintf(buf, len, "%s-%s", grp->name, t->name);
}

static bool known_test(const char *name) {
    for (int g = 0; g < NGROUPS; g++)
        for (int i = 0; i < GROUPS[g]->count; i++) {
            char full[64];
            test_name(full, sizeof full, GROUPS[g], &GROUPS[g]->tests[i]);
            if (!strcasecmp(name, full))
                return true;
        }
    return false;
}

// Every name TEST accepts, since it takes one at a time.
static void list_tests(void) {
    for (int g = 0; g < NGROUPS; g++)
        for (int i = 0; i < GROUPS[g]->count; i++) {
            const struct P96Test *t = &GROUPS[g]->tests[i];
            char full[64];
            test_name(full, sizeof full, GROUPS[g], t);
            if (t->palette_only)
                printf("%-24s palette only\n", full);
            else
                printf("%s\n", full);
        }
}

static bool validate_test(const char *name) {
    if (!name || known_test(name))
        return true;
    printf("no such testcase \"%s\"; try -h\n", name);
    return false;
}

#define MAX_REPORTED_DIFFS 8

// Wider than any P96 board will accept a bitmap, which is how the reference
// allocation is forced into fast memory. P96Tests uses 4100 for the same
// reason.
#define REFERENCE_WIDTH 4100

// Everything the command line settles, so the run loop never looks at argv
// again. parse_args() fills it; free_args() releases what it owns.
struct RunOpts {
    const char *test;       // the one testcase to run, or NULL for all of them
    const char *monitor;    // mode-name prefix, or NULL for the reference
    const char *dir;        // where this run's own images go
    const char *golden_dir; // where the references it compares against live
    SHORT w, h;             // scene: the region rendered and compared
    SHORT screen_w, screen_h;
    int depth;
    int bpp; // bytes per compared pixel: 1 (pen) or 3 (R8G8B8)
    ULONG threshold;
    bool capture;
    bool list_modes;
    bool list_tests;

    // Owned. The dir strings above may point into either of these, or into
    // rda's own storage, so all three outlive the run.
    struct RDArgs *rda;
    char *golden_buf, *output_buf;
};

static void free_args(struct RunOpts *o) {
    free(o->golden_buf);
    free(o->output_buf);
    if (o->rda)
        FreeArgs(o->rda);
}

// AmigaDOS answers "?" from the template alone, which gives the argument names
// but not what they mean. This is the rest of it.
static void usage(void) {
    printf(
        "\n"
        "  MONITOR        board to render on, e.g. Z3660 or PAL; softrast for\n"
        "                 P96's own software rasterizer, which is the reference\n"
        "  MODE           screen mode as WxHxD (default: the scene size at depth 8)\n"
        "  TEST/K         one testcase as <group>-<test>; all of them by default\n"
        "  CAPTURE/S      write the reference instead of comparing against it\n"
        "  OUTDIR/K       output directory (default output/<monitor>/<scene>x<depth>)\n"
        "  GOLDENDIR/K    reference directory (default golden/<scene>x<depth>)\n"
        "  SCENE/K        region rendered and compared, as WxH (default 320x200)\n"
        "  THRESHOLD/K/N  tolerate up to this many differing pixels\n"
        "  LISTMODES/S    dump the display database and exit\n"
        "  LISTTESTS/S    list the testcase names TEST accepts and exit\n"
        "  HELP/S         this text; -h and --help work too\n");
}

static bool parse_scene(const char *s, SHORT *w, SHORT *h) {
    int n, parsed_w, parsed_h;

    if (sscanf(s, "%dx%d%n", &parsed_w, &parsed_h, &n) != 2 || s[n] ||
        parsed_w <= 0 || parsed_h <= 0 ||
        parsed_w > SHRT_MAX || parsed_h > SHRT_MAX)
        return false;
    *w = (SHORT)parsed_w;
    *h = (SHORT)parsed_h;
    return true;
}

static bool parse_mode(const char *s, SHORT *w, SHORT *h, int *depth) {
    int n, parsed_w, parsed_h, parsed_depth;

    if (sscanf(s, "%dx%dx%d%n", &parsed_w, &parsed_h, &parsed_depth, &n) != 3 ||
        s[n] || parsed_w <= 0 || parsed_h <= 0 || parsed_depth < 1 ||
        parsed_depth > 32 ||
        parsed_w > SHRT_MAX || parsed_h > SHRT_MAX)
        return false;
    *w = (SHORT)parsed_w;
    *h = (SHORT)parsed_h;
    *depth = parsed_depth;
    return true;
}

// Settle everything the command line has to say. Returns RETURN_OK, or the
// code main should exit with having printed why. The caller free_args() either
// way once it is done with the strings, which point into what this allocates.
static int parse_args(struct RunOpts *o) {
    static const char *TEMPLATE =
        /* [0] = */ "MONITOR,"
        /* [1] = */ "MODE,"
        /* [2] = */ "TEST/K,"
        /* [3] = */ "CAPTURE/S,"
        /* [4] = */ "OUTDIR/K,"
        /* [5] = */ "GOLDENDIR/K,"
        /* [6] = */ "SCENE/K,"
        /* [7] = */ "THRESHOLD/K/N,"
        /* [8] = */ "LISTMODES/S,"
        /* [9] = */ "LISTTESTS/S,"
        /* [10] = */ "HELP=--help=-h/S";
    LONG args[11];

    memset(o, 0, sizeof *o);
    memset(args, 0, sizeof args);

    o->rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!o->rda) {
        PrintFault(IoErr(), (STRPTR)"p96cts");
        usage();
        return RETURN_FAIL;
    }

    printf("%s\n", VERSION_LINE);

    if (args[10]) {
        usage();
        return RETURN_WARN;
    }

    // Amiga low-res NTSC. Every primitive is testable at this size, and it
    // keeps the committed goldens small.
    o->w = 320;
    o->h = 200;
    o->depth = 8;
    o->test = args[2] ? (const char *)args[2] : NULL;
    o->capture = args[3] != 0;
    o->list_modes = args[8] != 0;
    o->list_tests = args[9] != 0;

    // The reference run is the absence of a board, which as a positional
    // argument needs a name of its own. Internally it stays NULL, which is
    // also what names the output directory.
    o->monitor = args[0] ? (const char *)args[0] : NULL;
    if (o->monitor && !strcmp(o->monitor, "softrast"))
        o->monitor = NULL;

    // MODE is not /A, though a run has to have one: /A is checked by ReadArgs
    // before anything else, which would make even -h and LISTMODES demand a
    // mode. So it is required here instead, where those have already been
    // dealt with.
    if (!o->list_modes && !o->list_tests && !args[1]) {
        printf("MONITOR and MODE are required, "
               "as in \"p96cts softrast 320x200x8\"\n");
        usage();
        return RETURN_ERROR;
    }

    if (args[6] && !parse_scene((const char *)args[6], &o->w, &o->h)) {
        printf("SCENE must be WxH\n");
        return RETURN_ERROR;
    }

    // MODE sizes the screen, SCENE the region actually rendered and compared.
    // They differ because a board need not offer a mode as small as the scene:
    // the smallest Z3660 mode is 640x400, so a 320x200 scene is drawn into the
    // corner of a larger screen and only that corner is compared. Goldens stay
    // small and portable across boards with different mode lists.
    if (args[1] && !parse_mode((const char *)args[1], &o->screen_w,
                               &o->screen_h, &o->depth)) {
        printf("MODE must be WxHxD with dimensions up to 32767 "
               "and depth 1 through 32\n");
        return RETURN_ERROR;
    }
    if (!o->screen_w)
        o->screen_w = o->w;
    if (!o->screen_h)
        o->screen_h = o->h;
    if (o->screen_w < o->w || o->screen_h < o->h) {
        printf("mode %dx%d is smaller than the %dx%d scene\n", o->screen_w,
               o->screen_h, o->w, o->h);
        return RETURN_ERROR;
    }

    if (args[7]) {
        LONG threshold = *(LONG *)args[7];
        if (threshold < 0) {
            printf("THRESHOLD must not be negative\n");
            return RETURN_ERROR;
        }
        o->threshold = (ULONG)threshold;
    }

    // 8 compares pen values; 24 compares R8G8B8, which any truecolor screen
    // canonicalizes to on readback. 15/16-bit modes are the deliberate gap:
    // their reference would have to be rendered in the same 5-6-5 precision,
    // not just converted to it, so they need their own path.
    if (o->depth != 8 && o->depth != 24) {
        printf("depth %d is not supported (8 or 24)\n", o->depth);
        return RETURN_ERROR;
    }
    o->bpp = o->depth > 8 ? 3 : 1;

    if (o->w & 15) {
        printf("width %d must be a multiple of 16\n", o->w);
        return RETURN_ERROR;
    }
    if (!validate_test(o->test))
        return RETURN_ERROR;

    // Goldens are named for what they contain -- the scene, at a depth -- so a
    // differently sized scene cannot overwrite an existing set, and a deeper
    // one can sit beside it later. Not the MODE: the same scene drawn into the
    // corner of a larger screen must compare equal to it on a screen its own
    // size, which is why the two are separate.
    //
    // Depth is enough while palette is the only pen-addressed format. It stops
    // being enough at 16-bit, where r5g6b5 and r5g5b5 differ, so a wider
    // comparison path wants the format in the name too.
    //
    // Run output is per monitor, so several boards can be compared against the
    // one reference set.
    if (asprintf(&o->golden_buf, "golden/%dx%dx%d", o->w, o->h, o->depth) < 0 ||
        asprintf(&o->output_buf, "output/%s/%dx%dx%d",
                 o->monitor ? o->monitor : "softrast",
                 o->w, o->h, o->depth) < 0) {
        printf("out of memory\n");
        return RETURN_FAIL;
    }
    o->golden_dir = args[5] ? (const char *)args[5] : o->golden_buf;
    o->dir = args[4] ? (const char *)args[4]
                     : (o->capture ? o->golden_dir : o->output_buf);
    return RETURN_OK;
}

// Keep what a failing scene rendered, and a picture of where it went wrong:
// <test>.fail.png is the render itself, <test>.diff.png marks the differing
// pixels in red over the golden dimmed to gray -- at full intensity the scene
// buries a few single-pixel diffs. Returns true if an image could not be
// written.
static bool write_failure_images(const char *name, const UBYTE *idx,
                                 const UBYTE *gold, const struct RunOpts *o) {
    bool failed = false;
    int bpp = o->bpp;

    char *path = image_path(o->dir, name, ".fail.png");
    if (!path || p96cts_write_png(path, idx, o->w, o->h, bpp))
        failed = true;
    else
        printf("       captured %s\n", path);
    free(path);

    UBYTE *d = AllocVec((ULONG)o->w * o->h * bpp, MEMF_CLEAR);
    if (!d) {
        printf("WARNING: failed to allocate diff buffer for %s\n", name);
        return failed;
    }
    for (SHORT y = 0; y < o->h; y++)
        for (SHORT x = 0; x < o->w; x++) {
            ULONG p = ((ULONG)y * o->w + x) * bpp;
            if (bpp != 3) {
                d[p] = (idx[p] != gold[p]) ? 2 : (gold[p] ? 5 : 0);
            } else if (memcmp(idx + p, gold + p, 3)) {
                d[p] = 255;
                d[p + 1] = d[p + 2] = 0;
            } else {
                UBYTE gray = (UBYTE)((gold[p] + gold[p + 1] + gold[p + 2]) / 6);
                d[p] = d[p + 1] = d[p + 2] = gray;
            }
        }

    path = image_path(o->dir, name, ".diff.png");
    if (!path || p96cts_write_png(path, d, o->w, o->h, bpp))
        failed = true;
    else
        printf("       wrote difference to %s\n", path);
    free(path);
    FreeVec(d);
    return failed;
}

// Prepare the shared RastPort for a testcase: lay a loud checkerboard into the
// scene, then reset the render state to a known default.
//
// The poison catches a test that fails to paint every pixel it compares -- it
// shows the leftover and fails against its golden, instead of passing on
// whatever the previous test left there. The state reset (draw mode, pens,
// write mask, line pattern) keeps a test from inheriting anything from whatever
// ran before it, so results cannot depend on the order tests run in.
static void reset_scene(struct RastPort *rp, const struct RunOpts *o) {
    const SHORT cell = 16;
    ULONG color[2] = {
        p96cts_color(0xAA, 0xFF00FFUL),
        p96cts_color(0x55, 0xFFFF00UL),
    };
    int row = 0;

    rp->Mask = 0xFF; // so the fill reaches every plane
    for (SHORT y = 0; y < o->h; y += cell) {
        SHORT y2 = y + cell - 1 < o->h ? y + cell - 1 : o->h - 1;
        int i = row;

        for (SHORT x = 0; x < o->w; x += cell) {
            SHORT x2 = x + cell - 1 < o->w ? x + cell - 1 : o->w - 1;

            p96cts_fill(rp, x, y, x2, y2, color[i]);
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
static bool run_test(const struct P96Test *t, const char *name,
                     struct RastPort *rp, const struct RunOpts *o) {
    bool failed = false;
    int bpp = o->bpp;
    ULONG pixels = (ULONG)o->w * o->h, bad = 0;
    SHORT gw, gh;

    reset_scene(rp, o);
    t->fn(rp, o->w, o->h);
    // Wait for the blitter before reading the scene back.
    WaitBlit();
    UBYTE *idx = bpp == 3 ? p96cts_read_rgb(rp, o->w, o->h)
                          : p96cts_read_pens(rp, o->w, o->h, o->depth);
    if (!idx) {
        printf("FAIL %-24s memory allocation failed\n", name);
        return true;
    }
    if (o->capture) {
        char *path = image_path(o->dir, name, ".png");
        if (!path || p96cts_write_png(path, idx, o->w, o->h, bpp))
            failed = true;
        else
            printf("captured %s\n", path);
        free(path);
        FreeVec(idx);
        return failed;
    }

    UBYTE *gold = NULL;
    {
        char *path = image_path(o->golden_dir, name, ".png");
        if (path)
            gold = p96cts_read_png(path, &gw, &gh, bpp);
        if (!gold) {
            printf("FAIL %-24s no golden at %s\n", name, path ? path : "?");
            free(path);
            FreeVec(idx);
            return true;
        }
        free(path);
    }

    if (gw != o->w || gh != o->h) {
        printf("FAIL %-24s golden is %dx%d, scene is %dx%d\n", name, gw, gh,
               o->w, o->h);
        failed = true;
    } else {
        for (SHORT y = 0; y < o->h; y++)
            for (SHORT x = 0; x < o->w; x++) {
                ULONG p = ((ULONG)y * o->w + x) * bpp;
                if (memcmp(idx + p, gold + p, bpp))
                    bad++;
            }
        if (bad > o->threshold) {
            printf("FAIL %-24s %lu of %lu pixels differ\n", name,
                   (unsigned long)bad, (unsigned long)pixels);
            failed = true;
        } else if (bad) {
            // Under THRESHOLD, so a pass -- but say how close it came.
            printf("PASS %-24s %lu pixels differ\n", name,
                   (unsigned long)bad);
        } else {
            printf("PASS %s\n", name);
        }
        if (bad) {
            // Hunting a handful of single pixels in a 320x200 image by eye is
            // hopeless, so name the first few outright.
            int shown = 0;
            for (SHORT y = 0; y < o->h && shown < MAX_REPORTED_DIFFS; y++)
                for (SHORT x = 0; x < o->w && shown < MAX_REPORTED_DIFFS; x++) {
                    ULONG p = ((ULONG)y * o->w + x) * bpp;
                    if (!memcmp(idx + p, gold + p, bpp))
                        continue;
                    if (bpp == 3)
                        printf("       at %3d,%3d golden %02X%02X%02X, "
                               "got %02X%02X%02X\n", x, y,
                               gold[p], gold[p + 1], gold[p + 2],
                               idx[p], idx[p + 1], idx[p + 2]);
                    else
                        printf("       at %3d,%3d golden %3d, got %3d\n", x, y,
                               gold[p], idx[p]);
                    shown++;
                }
            if (bad > (ULONG)shown)
                printf("       ... and %lu more\n", (unsigned long)(bad - shown));
        }
        if (bad)
            failed |= write_failure_images(name, idx, gold, o);
    }
    FreeVec(gold);
    FreeVec(idx);
    return failed;
}

int main(void) {
    struct RunOpts o;
    int failures = 0, rc = 0;
    ULONG id;
    struct Screen *scr = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp_off, *rp;

    rc = parse_args(&o);
    if (rc != RETURN_OK) {
        free_args(&o);
        return rc;
    }
    p96cts_truecolor = o.depth > 8;

    // Before the libraries, since it needs none of them.
    if (o.list_tests) {
        list_tests();
        goto out;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    P96Base = OpenLibrary((STRPTR)"Picasso96API.library", 2);
    if (!IntuitionBase || !GfxBase || !P96Base) {
        printf("failed to open libraries\n");
        rc = 20;
        goto out;
    }

    if (o.list_modes) {
        p96cts_list_modes();
        goto out;
    }

    // A screen is opened either way. The driver run renders into it, since
    // that is the thing under test. The reference run only borrows it: a pen
    // number is not a color on its own, and the mapping that turns it into one
    // comes from the screen, reaching the reference bitmap through the friend
    // argument below.
    //
    // The driver run needs the exact mode named. The reference run needs only
    // some screen of the right depth, and the scene size it renders at is
    // often not a real mode at all -- P96 publishes a 320x200 entry per pixel
    // format, but they are mode prefs templates that never open.
    if (o.monitor) {
        id = p96cts_find_mode(o.screen_w, o.screen_h, o.depth, o.monitor,
                              NULL, 0);
        if (id == P96CTS_INVALID_MODE) {
            printf("no %s mode %dx%dx%d in the display database\n", o.monitor,
                   o.screen_w, o.screen_h, o.depth);
            failures = 1;
            goto out;
        }
    } else {
        id = p96cts_find_mode(0, 0, o.depth, NULL, NULL, 0);
        if (id == P96CTS_INVALID_MODE) {
            printf("no %d-bit mode in the display database\n", o.depth);
            failures = 1;
            goto out;
        }
    }
    // SA_Pens with an empty spec so Intuition reserves none of them: every pen
    // belongs to the scenes. SA_Colors32 rather than SetRGB32 afterwards
    // because it takes precedence over every other way the palette gets set.
    {
        static WORD no_pens[] = {~0};
        const ULONG *colors = p96cts_palette();

        // The driver run is rendered on the screen, so it gets the size the
        // mode was asked for. The reference run only borrows the screen and
        // renders into its own bitmap, so it takes the mode's own dimensions:
        // STDSCREENWIDTH/HEIGHT ask for the DisplayClip rectangle.
        //
        // SA_Behind for the reference run: it renders into its own bitmap and
        // only borrows this screen, so there is nothing to see on it. Opening
        // it in front would just black out the display for the whole run.
        scr = OpenScreenTags(NULL, SA_DisplayID, id,
                             SA_Width, o.monitor ? o.screen_w : STDSCREENWIDTH,
                             SA_Height, o.monitor ? o.screen_h : STDSCREENHEIGHT,
                             SA_Depth, o.depth, SA_Pens, (ULONG)no_pens,
                             SA_Colors32, (ULONG)colors, SA_Quiet, TRUE,
                             SA_Behind, o.monitor ? FALSE : TRUE,
                             SA_ShowTitle, FALSE, TAG_DONE);
    }
    if (!scr) {
        printf("OpenScreen failed\n");
        failures = 1;
        goto out;
    }

    if (o.monitor) {
        rp = &scr->RastPort;
    } else {
        // Friended to the screen's bitmap, so pens resolve through the
        // screen's colors -- but wider than any board can hold, which is what
        // keeps it in fast memory anyway. BMF_USERPRIVATE alone does not: with
        // a friend and a width the board can take, P96 puts it on the card and
        // the run is no longer an independent reference. The trick, and the
        // width, are from P96Tests/DrawLine.c.
        //
        // Off the board, rtg.library rasterizes this itself: P96's own
        // software implementation of the same primitives, independent of any
        // card driver and of the blitter. Only the leftmost part is drawn on
        // and read back; the rest is there to make the allocation refuse the
        // card.
        bm = p96AllocBitMap(REFERENCE_WIDTH, o.screen_h, o.depth,
                            BMF_CLEAR | BMF_USERPRIVATE, scr->RastPort.BitMap,
                            o.depth > 8 ? RGBFB_R8G8B8 : RGBFB_CLUT);
        if (!bm) {
            printf("p96AllocBitMap %dx%dx%d failed\n", REFERENCE_WIDTH,
                   o.screen_h, o.depth);
            failures = 1;
            goto cleanup;
        }
        // Copied, not InitRastPort'd, for the same reason as the friend
        // bitmap: this RastPort belongs to the screen. Only the layer and the
        // bitmap are replaced, so drawing lands offscreen and unclipped.
        rp_off = scr->RastPort;
        rp_off.Layer = NULL;
        rp_off.BitMap = bm;
        rp = &rp_off;
    }

    // Ask the bitmap what it actually is rather than assuming from depth.
    // A depth-8 run compares pen values, so it needs a bitmap addressed by
    // pen: chunky RGBFB_CLUT on an RTG board, or RGBFB_NONE for the planar
    // bitmaps AGA gives -- the name is historical, and ReadPixelArray8 reads
    // either. A deeper run reads back through p96ReadPixelArray, which
    // converts any truecolor format to R8G8B8, so there it is those same two
    // that are refused.
    {
        ULONG fmt = p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT);
        bool by_pen = fmt == RGBFB_CLUT || fmt == RGBFB_NONE;

        if (o.depth <= 8 ? !by_pen : by_pen) {
            printf("mode is %s, which does not match depth %d\n",
                   p96cts_format_name(fmt), o.depth);
            failures = 1;
            goto cleanup;
        }
        if (!o.monitor && p96GetBitMapAttr(rp->BitMap, P96BMA_ISONBOARD)) {
            printf("reference bitmap landed on the board; it would not be "
                   "an independent reference\n");
            failures = 1;
            goto cleanup;
        }
    }

    printf("testing %s %dx%dx%d %s, scene %dx%d",
           o.monitor ? o.monitor : "P96 software rasterizer",
           o.screen_w, o.screen_h,
           o.depth, p96cts_format_name(p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT)),
           o.w, o.h);
    // Where a comparison reads from is determined by the scene, so it is not
    // worth a line; where a capture writes to is a side effect worth naming.
    if (o.capture)
        printf(", capturing to %s", o.golden_dir);
    printf("\n");

    make_path(o.dir);

    for (int g = 0; g < NGROUPS; g++) {
        for (int i = 0; i < GROUPS[g]->count; i++) {
            const struct P96Test *t = &GROUPS[g]->tests[i];
            char full[64];
            test_name(full, sizeof full, GROUPS[g], t);
            if (!selected(o.test, full))
                continue;
            if (p96cts_truecolor && t->palette_only) {
                printf("skip %s: palette only, it tests something truecolor "
                       "has no equivalent of\n", full);
                continue;
            }
            failures += run_test(t, full, rp, &o);
        }
    }

cleanup:
    p96cts_backdrop_free();
    // The bitmap goes first: it was allocated with the screen's as friend.
    if (bm)
        p96FreeBitMap(bm);
    if (scr)
        CloseScreen(scr);
out:
    if (P96Base)
        CloseLibrary(P96Base);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    free_args(&o);
    return rc ? rc : (failures ? 1 : 0);
}
