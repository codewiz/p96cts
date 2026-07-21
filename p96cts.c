/* SPDX-License-Identifier: 0BSD */

/* p96cts -- P96 driver conformance test suite.
 *
 * This file is the harness: arguments, the run loop, and the comparison.
 * Scenes live in tests/, the graphics.library and P96 calls in gfx.c.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p96cts.h"
#include "gfx.h"
#include "pngio.h"

/* Standard AmigaOS version tag, readable with the Version command. */
static const char VERSTAG[] = "$VER: p96cts 0.3 (21.7.2026)";
#define VERSION_LINE (VERSTAG + 6)

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static const struct P96TestGroup *const GROUPS[] = {
    &DrawLineGroup,
    &FillRectGroup,
    &CopyRectGroup,
};
#define NGROUPS ((int)(sizeof GROUPS / sizeof GROUPS[0]))

/* CreateDir() makes one level, so walk the path creating each component.
 * Components that already exist fail harmlessly with ERROR_OBJECT_EXISTS. */
static void make_path(const char *path) {
    char buf[256];
    int i;

    strncpy(buf, path, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (i = 0; buf[i]; i++) {
        BPTR lock;
        if (buf[i] != '/')
            continue;
        buf[i] = 0;
        if ((lock = CreateDir((STRPTR)buf)))
            UnLock(lock);
        buf[i] = '/';
    }
    {
        BPTR lock = CreateDir((STRPTR)buf);
        if (lock)
            UnLock(lock);
    }
}

/* --- run ------------------------------------------------------------------ */

static int selected(STRPTR *names, const char *name) {
    int i;

    if (!names || !names[0])
        return 1; /* no TEST given: run all */
    for (i = 0; names[i]; i++)
        if (!strcmp((const char *)names[i], name))
            return 1;
    return 0;
}

/* Compose a testcase's full name. Testcases are named for what they do
 * ("solid", "overlap-down") and the group supplies the subject, so the name
 * a user types and the name on disk are both "<group>-<test>". */
static void test_name(char *buf, size_t len, const struct P96TestGroup *grp,
                      const struct P96Test *t) {
    snprintf(buf, len, "%s-%s", grp->name, t->name);
}

static int known_test(const char *name) {
    int g, i;

    for (g = 0; g < NGROUPS; g++)
        for (i = 0; i < GROUPS[g]->count; i++) {
            char full[64];
            test_name(full, sizeof full, GROUPS[g], &GROUPS[g]->tests[i]);
            if (!strcmp(name, full))
                return 1;
        }
    return 0;
}

static int validate_tests(STRPTR *names) {
    int i;

    if (!names || !names[0])
        return 1;
    for (i = 0; names[i]; i++)
        if (!known_test((const char *)names[i])) {
            printf("unknown test: %s\n", names[i]);
            return 0;
        }
    return 1;
}

#define MAX_REPORTED_DIFFS 8

/* Wider than any P96 board will accept a bitmap, which is how the reference
 * allocation is forced into fast memory. P96Tests uses 4100 for the same
 * reason. */
#define REFERENCE_WIDTH 4100

struct RunOpts {
    const char *dir, *golden_dir;
    SHORT w, h;  /* scene: the region rendered and compared */
    int depth;
    int bpp; /* bytes per compared pixel: 1 (pen) or 3 (R8G8B8) */
    ULONG threshold;
    int capture, want_diff;
};

static int parse_scene(const char *s, SHORT *w, SHORT *h) {
    int n, parsed_w, parsed_h;

    if (sscanf(s, "%dx%d%n", &parsed_w, &parsed_h, &n) != 2 || s[n] ||
        parsed_w <= 0 || parsed_h <= 0 ||
        parsed_w > SHRT_MAX || parsed_h > SHRT_MAX)
        return 0;
    *w = (SHORT)parsed_w;
    *h = (SHORT)parsed_h;
    return 1;
}

static int parse_mode(const char *s, SHORT *w, SHORT *h, int *depth) {
    int n, parsed_w, parsed_h, parsed_depth;

    if (sscanf(s, "%dx%dx%d%n", &parsed_w, &parsed_h, &parsed_depth, &n) != 3 ||
        s[n] || parsed_w <= 0 || parsed_h <= 0 || parsed_depth < 1 ||
        parsed_depth > 32 ||
        parsed_w > SHRT_MAX || parsed_h > SHRT_MAX)
        return 0;
    *w = (SHORT)parsed_w;
    *h = (SHORT)parsed_h;
    *depth = parsed_depth;
    return 1;
}

/* Render one testcase and capture or compare it. Returns 1 on failure. */
/* `name` is the testcase's full "<group>-<test>" name: the group qualifies it,
 * so two groups can both have an "edges" scene and their images cannot
 * collide in golden/. */
static int run_test(const struct P96Test *t, const char *name,
                    struct RastPort *rp, const struct RunOpts *o) {
    SHORT gw, gh, x, y;
    int failed = 0, bpp = o->bpp;
    ULONG pixels = (ULONG)o->w * o->h, bad = 0;
    char path[256];

    t->fn(rp, o->w, o->h);
    UBYTE *idx = bpp == 3 ? p96cts_read_rgb(rp, o->w, o->h)
                          : p96cts_read_pens(rp, o->w, o->h, o->depth);
    if (!idx) {
        printf("FAIL %-24s memory allocation failed\n", name);
        return 1;
    }
    snprintf(path, sizeof path, "%s/%s.png", o->dir, name);

    if (o->capture) {
        if (p96cts_write_png(path, idx, o->w, o->h, bpp))
            failed = 1;
        else
            printf("captured %s\n", path);
        FreeVec(idx);
        return failed;
    }

    if (p96cts_write_png(path, idx, o->w, o->h, bpp)) {
        FreeVec(idx);
        return 1;
    }
    snprintf(path, sizeof path, "%s/%s.png", o->golden_dir, name);
    UBYTE *gold = p96cts_read_png(path, &gw, &gh, bpp);
    if (!gold) {
        printf("FAIL %-24s no golden at %s\n", name, path);
        FreeVec(idx);
        return 1;
    }

    if (gw != o->w || gh != o->h) {
        printf("FAIL %-24s golden is %dx%d, scene is %dx%d\n", name, gw, gh,
               o->w, o->h);
        failed = 1;
    } else {
        for (y = 0; y < o->h; y++)
            for (x = 0; x < o->w; x++) {
                ULONG p = ((ULONG)y * o->w + x) * bpp;
                if (memcmp(idx + p, gold + p, bpp))
                    bad++;
            }
        if (bad > o->threshold) {
            printf("FAIL %-24s %lu of %lu pixels differ\n", name,
                   (unsigned long)bad, (unsigned long)pixels);
            failed = 1;
        } else if (bad) {
            /* Under THRESHOLD, so a pass -- but say how close it came. */
            printf("PASS %-24s %lu pixels differ\n", name,
                   (unsigned long)bad);
        } else {
            printf("PASS %s\n", name);
        }
        if (bad) {
            /* Hunting a handful of single pixels in a 320x200 image by eye is
             * hopeless, so name the first few outright. */
            int shown = 0;
            for (y = 0; y < o->h && shown < MAX_REPORTED_DIFFS; y++)
                for (x = 0; x < o->w && shown < MAX_REPORTED_DIFFS; x++) {
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
        if (o->want_diff && bad) {
            /* Differing pixels in red over the golden scene dimmed to gray:
             * at full intensity the scene buries a few single-pixel diffs. */
            UBYTE *d = AllocVec(pixels * bpp, MEMF_CLEAR);
            if (!d) {
                printf("WARNING: failed to allocate diff buffer for %s\n", name);
            } else {
                for (y = 0; y < o->h; y++)
                    for (x = 0; x < o->w; x++) {
                        ULONG p = ((ULONG)y * o->w + x) * bpp;
                        if (bpp == 3) {
                            if (memcmp(idx + p, gold + p, 3)) {
                                d[p] = 255;
                                d[p + 1] = d[p + 2] = 0;
                            } else {
                                /* The golden pixel, dimmed to a gray. */
                                UBYTE gray = (UBYTE)((gold[p] + gold[p + 1] +
                                                      gold[p + 2]) / 6);
                                d[p] = d[p + 1] = d[p + 2] = gray;
                            }
                        } else {
                            d[p] = (idx[p] != gold[p]) ? 2 : (gold[p] ? 5 : 0);
                        }
                    }
                snprintf(path, sizeof path, "%s/%s.diff.png", o->dir, name);
                if (p96cts_write_png(path, d, o->w, o->h, bpp))
                    failed = 1;
                FreeVec(d);
            }
        }
    }
    FreeVec(gold);
    FreeVec(idx);
    return failed;
}

int main(void) {
    static const char *TEMPLATE =
        "TEST/M,CAPTURE/S,MONITOR/K,DIR/K,GOLDEN/K,MODE/K,SCENE/K,"
        "THRESHOLD/K/N,DIFF/S,LISTMODES/S";
    LONG args[10];
    struct RDArgs *rda;
    struct RunOpts o;
    const char *monitor;
    int failures = 0, rc = 0, g, i;
    SHORT screen_w = 0, screen_h = 0;
    ULONG id;
    struct Screen *scr = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp_off, *rp;
    char golden_buf[64], output_buf[64];

    memset(args, 0, sizeof args);
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"p96cts");
        return 20;
    }

    printf("%s\n", VERSION_LINE);

    memset(&o, 0, sizeof o);
    /* Amiga low-res NTSC. Every primitive is testable at this size, and it
     * keeps the committed goldens small. */
    o.w = 320;
    o.h = 200;
    o.depth = 8;
    o.capture = args[1] != 0;
    monitor = args[2] ? (const char *)args[2] : NULL;
    if (args[6] && !parse_scene((const char *)args[6], &o.w, &o.h)) {
        printf("SCENE must be WxH\n");
        FreeArgs(rda);
        return 5;
    }

    /* MODE sizes the screen, SCENE the region actually rendered and compared.
     * They differ because a board need not offer a mode as small as the scene:
     * the smallest Z3660 mode is 640x400, so a 320x200 scene is drawn into the
     * corner of a larger screen and only that corner is compared. Goldens stay
     * small and portable across boards with different mode lists. */
    if (args[5] && !parse_mode((const char *)args[5], &screen_w, &screen_h,
                               &o.depth)) {
        printf("MODE must be WxHxD with dimensions up to 32767 and depth 1 through 32\n");
        FreeArgs(rda);
        return 5;
    }
    if (!screen_w)
        screen_w = o.w;
    if (!screen_h)
        screen_h = o.h;
    if (screen_w < o.w || screen_h < o.h) {
        printf("mode %dx%d is smaller than the %dx%d scene\n", screen_w, screen_h,
               o.w, o.h);
        FreeArgs(rda);
        return 5;
    }

    if (args[7]) {
        LONG threshold = *(LONG *)args[7];
        if (threshold < 0) {
            printf("THRESHOLD must not be negative\n");
            FreeArgs(rda);
            return 5;
        }
        o.threshold = (ULONG)threshold;
    }
    o.want_diff = args[8] != 0;

    /* 8 compares pen values; 24 compares R8G8B8, which any truecolor screen
     * canonicalizes to on readback. 15/16-bit modes are the deliberate gap:
     * their reference would have to be rendered in the same 5-6-5 precision,
     * not just converted to it, so they need their own path. */
    if (o.depth != 8 && o.depth != 24) {
        printf("depth %d is not supported (8 or 24)\n", o.depth);
        FreeArgs(rda);
        return 5;
    }
    o.bpp = o.depth > 8 ? 3 : 1;
    p96cts_truecolor = o.depth > 8;

    if (o.w & 15) {
        printf("width %d must be a multiple of 16\n", o.w);
        FreeArgs(rda);
        return 5;
    }
    if (!validate_tests((STRPTR *)args[0])) {
        FreeArgs(rda);
        return 5;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    P96Base = OpenLibrary((STRPTR)"Picasso96API.library", 2);
    if (!IntuitionBase || !GfxBase || !P96Base) {
        printf("failed to open libraries\n");
        rc = 20;
        goto out;
    }

    if (args[9]) {
        p96cts_list_modes();
        goto out;
    }

    /* A screen is opened either way. The driver run renders into it, since
     * that is the thing under test. The reference run only borrows it: a pen
     * number is not a color on its own, and the mapping that turns it into one
     * comes from the screen, reaching the reference bitmap through the friend
     * argument below.
     *
     * The driver run needs the exact mode named. The reference run needs only
     * some screen of the right depth, and the scene size it renders at is
     * often not a real mode at all -- P96 publishes a 320x200 entry per pixel
     * format, but they are mode prefs templates that never open. */
    if (monitor) {
        id = p96cts_find_mode(screen_w, screen_h, o.depth, monitor, NULL, 0);
        if (id == P96CTS_INVALID_MODE) {
            printf("no %s mode %dx%dx%d in the display database\n", monitor,
                   screen_w, screen_h, o.depth);
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
    /* SA_Pens with an empty spec so Intuition reserves none of them: every pen
     * belongs to the scenes. SA_Colors32 rather than SetRGB32 afterwards
     * because it takes precedence over every other way the palette gets set. */
    {
        static WORD no_pens[] = {~0};
        const ULONG *colors = p96cts_palette();

        /* The driver run is rendered on the screen, so it gets the size the
         * mode was asked for. The reference run only borrows the screen and
         * renders into its own bitmap, so it takes the mode's own dimensions:
         * STDSCREENWIDTH/HEIGHT ask for the DisplayClip rectangle. */
        scr = OpenScreenTags(NULL, SA_DisplayID, id,
                             SA_Width, monitor ? screen_w : STDSCREENWIDTH,
                             SA_Height, monitor ? screen_h : STDSCREENHEIGHT,
                             SA_Depth, o.depth, SA_Pens, (ULONG)no_pens,
                             SA_Colors32, (ULONG)colors, SA_Quiet, TRUE,
                             SA_ShowTitle, FALSE, TAG_DONE);
    }
    if (!scr) {
        printf("OpenScreen failed\n");
        failures = 1;
        goto out;
    }

    if (monitor) {
        rp = &scr->RastPort;
    } else {
        /* Friended to the screen's bitmap, so pens resolve through the
         * screen's colors -- but wider than any board can hold, which is what
         * keeps it in fast memory anyway. BMF_USERPRIVATE alone does not: with
         * a friend and a width the board can take, P96 puts it on the card and
         * the run is no longer an independent reference. The trick, and the
         * width, are from P96Tests/DrawLine.c.
         *
         * Off the board, rtg.library rasterizes this itself: P96's own
         * software implementation of the same primitives, independent of any
         * card driver and of the blitter. Only the leftmost part is drawn on
         * and read back; the rest is there to make the allocation refuse the
         * card. */
        bm = p96AllocBitMap(REFERENCE_WIDTH, screen_h, o.depth,
                            BMF_CLEAR | BMF_USERPRIVATE, scr->RastPort.BitMap,
                            o.depth > 8 ? RGBFB_R8G8B8 : RGBFB_CLUT);
        if (!bm) {
            printf("p96AllocBitMap %dx%dx%d failed\n", REFERENCE_WIDTH,
                   screen_h, o.depth);
            failures = 1;
            goto cleanup;
        }
        /* Copied, not InitRastPort'd, for the same reason as the friend
         * bitmap: this RastPort belongs to the screen. Only the layer and the
         * bitmap are replaced, so drawing lands offscreen and unclipped. */
        rp_off = scr->RastPort;
        rp_off.Layer = NULL;
        rp_off.BitMap = bm;
        rp = &rp_off;
    }

    /* Ask the bitmap what it actually is rather than assuming from depth.
     * A depth-8 run compares pen values, so it needs a bitmap addressed by
     * pen: chunky RGBFB_CLUT on an RTG board, or RGBFB_NONE for the planar
     * bitmaps AGA gives -- the name is historical, and ReadPixelArray8 reads
     * either. A deeper run reads back through p96ReadPixelArray, which
     * converts any truecolor format to R8G8B8, so there it is those same two
     * that are refused. */
    {
        ULONG fmt = p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT);
        int by_pen = fmt == RGBFB_CLUT || fmt == RGBFB_NONE;

        if (o.depth <= 8 ? !by_pen : by_pen) {
            printf("mode is %s, which does not match depth %d\n",
                   p96cts_format_name(fmt), o.depth);
            failures = 1;
            goto cleanup;
        }
        if (!monitor && p96GetBitMapAttr(rp->BitMap, P96BMA_ISONBOARD)) {
            printf("reference bitmap landed on the board; it would not be "
                   "an independent reference\n");
            failures = 1;
            goto cleanup;
        }
        /* Goldens are named for what they contain -- the scene, at a depth --
         * so a differently sized scene cannot overwrite an existing set, and
         * a deeper one can sit beside it later. Not the MODE: the same scene
         * drawn into the corner of a larger screen must compare equal to it
         * on a screen its own size, which is why the two are separate.
         *
         * Depth is enough while clut is the only supported format. It stops
         * being enough at 16-bit, where r5g6b5 and r5g5b5 differ, so a wider
         * comparison path wants the format in the name too.
         *
         * Run output is per monitor, so several boards can be compared
         * against the one reference set. */
        snprintf(golden_buf, sizeof golden_buf, "golden/%dx%dx%d", o.w, o.h, o.depth);
        snprintf(output_buf, sizeof output_buf, "output/%s/%dx%dx%d",
                 monitor ? monitor : "softrast", o.w, o.h, o.depth);
        o.golden_dir = args[4] ? (const char *)args[4] : golden_buf;
        o.dir = args[3] ? (const char *)args[3]
                        : (o.capture ? o.golden_dir : output_buf);
    }

    printf("testing %s %dx%dx%d %s, scene %dx%d",
           monitor ? monitor : "P96 software rasterizer", screen_w, screen_h,
           o.depth, p96cts_format_name(p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT)),
           o.w, o.h);
    /* Where a comparison reads from is determined by the scene, so it is not
     * worth a line; where a capture writes to is a side effect worth naming. */
    if (o.capture)
        printf(", capturing to %s", o.golden_dir);
    printf("\n");

    make_path(o.dir);

    for (g = 0; g < NGROUPS; g++) {
        for (i = 0; i < GROUPS[g]->count; i++) {
            const struct P96Test *t = &GROUPS[g]->tests[i];
            char full[64];
            test_name(full, sizeof full, GROUPS[g], t);
            if (!selected((STRPTR *)args[0], full))
                continue;
            if (p96cts_truecolor && t->clut_only) {
                printf("skip %s: palette-only, it tests something truecolor "
                       "has no equivalent of\n", full);
                continue;
            }
            failures += run_test(t, full, rp, &o);
        }
    }

cleanup:
    /* The bitmap goes first: it was allocated with the screen's as friend. */
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
    FreeArgs(rda);
    return rc ? rc : (failures ? 1 : 0);
}
