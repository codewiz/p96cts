// SPDX-License-Identifier: 0BSD
//
// p96cts -- P96 driver conformance test suite.
//
// This file is the harness: arguments, mode selection, and the run loop.
// Rendering and judging one testcase is runtest.c; scenes live in tests/,
// the graphics.library calls in gfx.c, and everything that needs an RTG
// library in rtg.c.

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp()

#include "p96cts.h"
#include "backdrop.h"
#include "countof.h"
#include "gfx.h"
#include "layer.h"
#include "modes.h"
#include "palette.h"
#include "report.h"
#include "runtest.h"
#include "timer.h"
#include "rtg.h"

// Standard AmigaOS version tag, readable with the Version command.
static const char VERSTAG[] =
        "$VER: p96cts " AMIGA_VERSION " (" AMIGA_DATE ") by Bernie Innocenti";
#define VERSION_LINE (VERSTAG + 6)

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

// Disarm libnix's Ctrl-C check, which every printf runs:
// it exits orphaning the screen and its bitmap.
void __chkabort(void) {}

// The check the run loop makes instead, between testcases.
static bool user_break(void) {
    if (!(SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C))
        return false;
    rpt(RPT_INFO, "***Break");
    return true;
}

static const struct P96TestGroup *const GROUPS[] = {
    &DrawLineGroup,
    &RectFillGroup,
    &ClipBlitGroup,
    &BltTemplateGroup,
    &BltPatternGroup,
    &BltBitMapGroup,
    &BitMapScaleGroup,
    &ScrollRasterGroup,
    &PixelArrayGroup,
};
#define NGROUPS ((int)countof(GROUPS))

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
            else if (t->truecolor_only)
                printf("%-24s truecolor only\n", full);
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

// Everything the command line settles, so the run loop never looks at argv
// again. parse_args() fills it; free_args() releases what it owns.
struct RunOpts {
    const char *test;        // the one testcase to run, or NULL for all of them
    const char *monitor;     // mode-name prefix, or NULL for the reference
    const char *dir;         // where this run's own images go
    const char *golden_dir;  // where the references it compares against live
    const char *report_file; // splice the run's report here too, or NULL
    SHORT w, h;             // scene: the region rendered and compared
    SHORT screen_w, screen_h;
    int depth;
    int bpp; // bytes per compared pixel: 1 (pen) or 3 (R8G8B8)
    bool capture;
    bool layer; // draw through a Layer rather than a bare RastPort
    bool clip;  // lay two obscuring layers over the scene; implies layer
    bool all_depths; // no MODE given: run every depth the monitor offers

    bool list_modes;
    bool list_tests;

    // GOLDENDIR/OUTDIR as given, kept apart from golden_dir/dir: those are
    // recomposed per depth on an all-depths run and reapply the overrides.
    const char *golden_override, *out_override;

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

// Set the run's depth and (re)compose the directories that carry it. An
// all-depths run calls this once per pass; the overrides win every time.
//
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
// A layered run renders the same scenes down a different path, so its
// images get a directory of their own rather than overwriting the
// unlayered ones: the two are meant to be compared against each other.
// A clipped run compares against the same goldens: the pixels the clip
// layers cover are excluded from the comparison instead.
static int set_depth(struct RunOpts *o, int depth) {
    o->depth = depth;
    o->bpp = depth > 8 ? 3 : 1;
    free(o->golden_buf);
    free(o->output_buf);
    o->golden_buf = o->output_buf = NULL;
    if (asprintf(&o->golden_buf, "golden/%dx%dx%d", o->w, o->h, depth) < 0 ||
        asprintf(&o->output_buf, "output/%s/%dx%dx%d%s",
                 o->monitor ? o->monitor : "softrast", o->w, o->h, depth,
                 o->clip ? "-clipped" : o->layer ? "-layered" : "") < 0) {
        printf("out of memory\n");
        return RETURN_FAIL;
    }
    o->golden_dir = o->golden_override ? o->golden_override : o->golden_buf;
    o->dir = o->out_override ? o->out_override
                             : (o->capture ? o->golden_dir : o->output_buf);
    return RETURN_OK;
}

// AmigaDOS answers "?" from the template alone, which gives the argument names
// but not what they mean. This is the rest of it.
static void usage(void) {
    printf(
        "\n"
        "  MONITOR        board to render on, e.g. Z3660 or PAL; softrast for\n"
        "                 P96's own software rasterizer, which is the reference\n"
        "  MODE           screen mode as WxHxD; without one, every supported\n"
        "                 depth runs at the smallest mode containing the scene\n"
        "  TEST/K         one testcase as <group>-<test>; all of them by default\n"
        "  CAPTURE/S      write the reference instead of comparing against it\n"
        "  LAYER/S        draw through a Layer covering the whole bitmap\n"
        "  CLIP/S         also lay two thin layers over the scene, so drawing\n"
        "                 is genuinely split against ClipRects; implies LAYER\n"
        "  OUTDIR/K       output directory (default output/<monitor>/<scene>x<depth>)\n"
        "  GOLDENDIR/K    reference directory (default golden/<scene>x<depth>)\n"
        "  SCENE/K        region rendered and compared, as WxH (default 320x200)\n"
        "  REPORTFILE/K   also write the report to this file\n"
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
        /* [4] = */ "LAYER/S,"
        /* [5] = */ "CLIP/S,"
        /* [6] = */ "OUTDIR/K,"
        /* [7] = */ "GOLDENDIR/K,"
        /* [8] = */ "SCENE/K,"
        /* [9] = */ "REPORTFILE/K,"
        /* [10] = */ "LISTMODES/S,"
        /* [11] = */ "LISTTESTS/S,"
        /* [12] = */ "HELP=--help=-h/S";
    LONG args[13];

    memset(o, 0, sizeof *o);
    memset(args, 0, sizeof args);

    o->rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!o->rda) {
        PrintFault(IoErr(), (STRPTR)"p96cts");
        usage();
        return RETURN_FAIL;
    }

    if (args[12]) {
        printf("%s\n", VERSION_LINE);
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
    o->clip = args[5] != 0;
    o->layer = args[4] != 0 || o->clip; // the clip layers need one to obscure
    if (o->clip && o->capture) {
        // The comparison excludes the covered pixels, so clipped runs use the
        // ordinary goldens; a capture would record the clip layers into them.
        printf("CAPTURE and CLIP do not combine; capture without CLIP\n");
        return RETURN_ERROR;
    }
    o->report_file = args[9] ? (const char *)args[9] : NULL;
    o->list_modes = args[10] != 0;
    o->list_tests = args[11] != 0;

    // The reference run is the absence of a board, which as a positional
    // argument needs a name of its own. Internally it stays NULL, which is
    // also what names the output directory.
    o->monitor = args[0] ? (const char *)args[0] : NULL;
    if (o->monitor && !strcmp(o->monitor, "softrast"))
        o->monitor = NULL;

    // MONITOR is not /A, though a run has to have one: /A is checked by
    // ReadArgs before anything else, which would make even -h and LISTMODES
    // demand it. So it is required here instead, where those have already
    // been dealt with. MODE is genuinely optional: without one, the run
    // covers every depth the monitor offers.
    if (!o->list_modes && !o->list_tests && !args[0]) {
        printf("MONITOR is required, as in \"p96cts softrast\"\n");
        usage();
        return RETURN_ERROR;
    }
    o->all_depths = !args[1];

    if (args[8] && !parse_scene((const char *)args[8], &o->w, &o->h)) {
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

    // 8 compares pen values; truecolor depths compare R8G8B8, which readback
    // canonicalizes to. 15/16-bit runs render their reference in the screen's
    // own quantized format (see reference_format() in rtg.c), so both sides
    // lose precision identically and keep their own golden sets.
    if (o->depth != 8 && o->depth != 15 && o->depth != 16 && o->depth != 24) {
        printf("depth %d is not supported (8, 15, 16 or 24)\n", o->depth);
        return RETURN_ERROR;
    }

    if (o->w & 15) {
        printf("width %d must be a multiple of 16\n", o->w);
        return RETURN_ERROR;
    }
    if (!validate_test(o->test))
        return RETURN_ERROR;

    o->golden_override = args[7] ? (const char *)args[7] : NULL;
    o->out_override = args[6] ? (const char *)args[6] : NULL;
    return set_depth(o, o->depth);
}

// Open the screen for one mode, run every selected testcase on it, and tear
// it back down. Returns the number of failures, or -1 on a user break.
static int run_mode(struct RunOpts *o, ULONG id) {
    int failures = 0;
    bool broke = false;
    struct Screen *scr = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp_off, *rp;
    const struct TestOpts topts = {
        .w = o->w,
        .h = o->h,
        .screen_w = o->screen_w,
        .screen_h = o->screen_h,
        .depth = o->depth,
        .bpp = o->bpp,
        .capture = o->capture,
        .clip = o->clip,
        .dir = o->dir,
        .golden_dir = o->golden_dir,
    };

    gfx_truecolor = o->depth > 8;

    // A screen is opened either way. The driver run renders into it, since
    // that is the thing under test. The reference run only borrows it: a pen
    // number is not a color on its own, and the mapping that turns it into one
    // comes from the screen, reaching the reference bitmap through the friend
    // argument below -- unfriended, a truecolor screen reads every pen as black.
    //
    // SA_Pens with an empty spec so Intuition reserves none of them: every pen
    // belongs to the scenes. The whole palette is set explicitly, since
    // Intuition seeds only pens 0-3 and 17-19 from Preferences and a truecolor
    // reference would otherwise record the capture machine's colors. SA_Colors32
    // rather than SetRGB32 afterwards, because it takes precedence over every
    // other way the palette gets set.
    {
        static WORD no_pens[] = {~0};
        const ULONG *colors = palette();

        // The driver run is rendered on the screen, so it gets the size the
        // mode was asked for. The reference run only borrows the screen and
        // renders into its own bitmap, so it takes the mode's own dimensions:
        // STDSCREENWIDTH/HEIGHT ask for the DisplayClip rectangle.
        //
        // SA_Behind for the reference run: it renders into its own bitmap and
        // only borrows this screen, so there is nothing to see on it. Opening
        // it in front would just black out the display for the whole run.
        scr = OpenScreenTags(NULL, SA_DisplayID, id,
                             SA_Width, o->monitor ? o->screen_w : STDSCREENWIDTH,
                             SA_Height, o->monitor ? o->screen_h : STDSCREENHEIGHT,
                             SA_Depth, o->depth, SA_Pens, (ULONG)no_pens,
                             SA_Colors32, (ULONG)colors, SA_Quiet, TRUE,
                             SA_Behind, o->monitor ? FALSE : TRUE,
                             SA_ShowTitle, FALSE,
                             TAG_DONE);
    }
    if (!scr) {
        rpt_errorf("OpenScreen failed");
        return 1;
    }

    if (o->monitor) {
        rp = &scr->RastPort;
    } else {
        bm = rtg_alloc_reference(scr, o->screen_h, o->depth);
        if (!bm) {
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

    // Ask the bitmap what it actually is rather than assuming from depth. A
    // depth-8 run compares pen values and so needs a pen-addressed bitmap;
    // a deeper one reads back as R8G8B8 and so needs anything but.
    {
        const ULONG fmt = rtg_rgbformat(rp->BitMap);
        const bool by_pen = rtg_format_by_pen(fmt);
        const bool want_pen = o->depth <= 8;

        if (by_pen != want_pen) {
            rpt_errorf("mode is %s, which does not match depth %d",
                          rtg_format_name(fmt), o->depth);
            failures = 1;
            goto cleanup;
        }
    }

    // The mode asked for is not always the mode that opens, and drawing into
    // scr->RastPort is unclipped.
    if (o->monitor && (scr->Width < o->w || scr->Height < o->h)) {
        rpt_errorf("screen opened %dx%d, smaller than the %dx%d scene",
                      scr->Width, scr->Height, o->w, o->h);
        failures = 1;
        goto cleanup;
    }

    // Every scene so far is drawn through a RastPort with no Layer, which is
    // the unclipped path. An application draws into a window, so its calls
    // reach the driver split against a ClipRect list instead. This layer
    // covers the whole bitmap and so clips nothing, which makes the same
    // golden set the assertion: the two paths must produce identical pixels.
    if (o->layer) {
        struct RastPort *layer_rp = layer_install(rp->BitMap);

        if (!layer_rp) {
            failures = 1;
            goto cleanup;
        }
        rp = layer_rp;
        // The obscuring clip layers go up last, in front of the drawing
        // layer, so every call a scene makes really is split against
        // several ClipRects.
        if (o->clip && !layer_clip(rp->BitMap, o->w, o->h)) {
            failures = 1;
            goto cleanup;
        }
    }

    rpt(RPT_INFO, "");
    rptf("testing %s %dx%dx%d %s, scene %dx%d%s",
            o->monitor ? o->monitor : "softrast",
            o->screen_w, o->screen_h, o->depth,
            rtg_format_name(rtg_rgbformat(rp->BitMap)),
            o->w, o->h,
            o->clip ? " (clipped)" : o->layer ? " (layered)" : "");
    if (o->monitor && (scr->Width != o->screen_w || scr->Height != o->screen_h))
        rptf("screen opened %dx%d instead", scr->Width, scr->Height);
    // Where a comparison reads from is determined by the scene, so it is not
    // worth a line; where a capture writes to is a side effect worth naming.
    if (o->capture)
        rptf("capturing to %s", o->golden_dir);

    for (int g = 0; g < NGROUPS; g++) {
        for (int i = 0; i < GROUPS[g]->count; i++) {
            const struct P96Test *t = &GROUPS[g]->tests[i];
            char full[64];

            if (user_break()) {
                broke = true;
                goto cleanup;
            }

            test_name(full, sizeof full, GROUPS[g], t);
            if (!selected(o->test, full))
                continue;
            if (gfx_truecolor && t->palette_only) {
                rpt_skip(full, "palette only");
                continue;
            }
            if (!gfx_truecolor && t->truecolor_only) {
                rpt_skip(full, "truecolor only");
                continue;
            }
            if (o->clip && t->no_clip) {
                rpt_skip(full, "not clippable");
                continue;
            }
            failures += run_test(t, full, rp, &topts);
        }
    }

cleanup:
    backdrop_free();
    // Before the bitmap it was created over.
    layer_free();
    // The bitmap goes first: it was allocated with the screen's as friend.
    rtg_free_bitmap(bm);
    CloseScreen(scr);
    return broke ? -1 : failures;
}

int main(void) {
    struct RunOpts o;
    int failures = 0, rc = 0;

    rc = parse_args(&o);
    if (rc != RETURN_OK) {
        free_args(&o);
        return rc;
    }

    if (o.report_file && !rpt_open(o.report_file)) {
        printf("cannot create %s\n", o.report_file);
        rc = RETURN_ERROR;
        goto out;
    }
    rptf("%s", VERSION_LINE);

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (!IntuitionBase) {
        rpt_errorf("failed to open intuition.library 39");
        rc = 20;
        goto out;
    }
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    if (!GfxBase) {
        rpt_errorf("failed to open graphics.library 39");
        rc = 20;
        goto out;
    }
    if (!rtg_open()) {
        rc = 20;
        goto out;
    }
    // The rest of the report header: the versions of everything between the
    // suite and the board, so a report is its own provenance record.
    rpt_libver((struct Library *)SysBase);
    rpt_libver((struct Library *)GfxBase);
    rtg_versions();
    if (!timer_open()) {
        rpt_errorf("failed to open timer.device");
        rc = 20;
        goto out;
    }

    if (o.list_tests) {
        list_tests();
        goto out;
    }

    if (o.list_modes) {
        list_modes();
        goto out;
    }

    if (o.all_depths) {
        // Every depth the run supports, each at the smallest mode that
        // contains the scene. A depth the monitor does not offer is noted and
        // skipped: boards legitimately lack pixel formats.
        static const int DEPTHS[] = {8, 15, 16, 24};

        for (int i = 0; i < (int)countof(DEPTHS); i++) {
            const int depth = DEPTHS[i];
            ULONG id;

            if (o.monitor) {
                int mw = 0, mh = 0;

                id = pick_mode(o.w, o.h, depth, o.monitor, &mw, &mh);
                if (id == INVALID_MODE) {
                    rptf("no %d-bit %s mode, skipped", depth, o.monitor);
                    continue;
                }
                o.screen_w = (SHORT)mw;
                o.screen_h = (SHORT)mh;
            } else {
                o.screen_w = o.w;
                o.screen_h = o.h;
                id = find_mode(0, 0, depth, NULL, NULL, 0);
                if (id == INVALID_MODE) {
                    rptf("no %d-bit mode in the display database, skipped",
                         depth);
                    continue;
                }
            }
            if (set_depth(&o, depth) != RETURN_OK) {
                rc = RETURN_FAIL;
                goto out;
            }

            const int r = run_mode(&o, id);
            if (r < 0) {
                rc = RETURN_WARN;
                goto out;
            }
            failures += r;
        }
    } else {
        ULONG id;

        // The driver run needs the exact mode named. The reference run needs
        // only some screen of the right depth, and the scene size it renders
        // at is often not a real mode at all -- P96 publishes a 320x200 entry
        // per pixel format, but they are mode prefs templates that never open.
        if (o.monitor) {
            id = find_mode(o.screen_w, o.screen_h, o.depth, o.monitor, NULL, 0);
            if (id == INVALID_MODE) {
                rpt_errorf("no %s mode %dx%dx%d in the display database",
                              o.monitor, o.screen_w, o.screen_h, o.depth);
                failures = 1;
                goto out;
            }
        } else {
            id = find_mode(0, 0, o.depth, NULL, NULL, 0);
            if (id == INVALID_MODE) {
                rpt_errorf("no %d-bit mode in the display database", o.depth);
                failures = 1;
                goto out;
            }
        }

        const int r = run_mode(&o, id);
        if (r < 0)
            rc = RETURN_WARN;
        else
            failures += r;
    }

out:
    timer_close();
    rtg_close();
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    rpt_close();
    free_args(&o);
    return rc ? rc : (failures ? 1 : 0);
}
