/* SPDX-License-Identifier: 0BSD */

/* p96cts -- P96 driver conformance test suite.
 *
 * Renders a table of scenes, reads each back as chunky pen values, and either
 * captures it as a golden image or compares it against a previously captured
 * one, reporting per-pixel differences. Exit code is non-zero if any testcase
 * fails, so a run is usable as an automated check.
 *
 * The reference and the subject are the same scenes rendered through
 * different paths:
 *
 *   MONITOR=<name>  open a screen on that monitor. For an RTG monitor this is
 *                   what exercises the card driver's blitter callbacks.
 *   (no MONITOR)    render into a BMF_USERPRIVATE bitmap, which no board
 *                   touches, so P96 rasterises it in software. That is the
 *                   reference: P96's own implementation of the primitives,
 *                   at any resolution, independent of card driver and blitter.
 *
 * Goldens live in golden/<pixel format>/ and are only comparable within one
 * format; a run's own images go to output/<monitor>/, so several boards can be
 * compared against the same reference set. DIR and GOLDEN override either.
 *
 *   p96cts CAPTURE                capture the reference into golden/clut/
 *   p96cts MONITOR=Z3660 DIFF     run on the board, compare against
 *                                 golden/clut/, write output/Z3660/<test>.png
 *                                 and, on mismatch, <test>.diff.png
 *   p96cts LIST                   dump the display database and exit
 *
 * TEST/M names the testcases to run; all of them by default.
 *
 * Screen width must be a multiple of 16 (ReadPixelArray8 granularity).
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <intuition/screens.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p96cts.h"

/* Standard AmigaOS version tag, readable with the Version command. */
static const char VERSTAG[] = "$VER: p96cts 0.1 (19.7.2026)";
#define VERSION_LINE (VERSTAG + 6)

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *P96Base;

static const struct P96TestGroup *const GROUPS[] = {
    &DrawLineGroup,
};
#define NGROUPS ((int)(sizeof GROUPS / sizeof GROUPS[0]))

void p96cts_clear(struct RastPort *rp, int w, int h, int pen) {
    SetDrMd(rp, JAM1);
    SetAPen(rp, pen);
    RectFill(rp, 0, 0, w - 1, h - 1);
}

/* --- display database ----------------------------------------------------- */

/* graphics/modeid.h defines INVALID_ID as ~0, an int, so comparing it against
 * a ULONG display id is a signedness mismatch. */
#define INVALID ((ULONG)INVALID_ID)

/* Find a display id of the given size/depth. `monitor` selects by mode-name
 * prefix ("PAL", "Z3660", ...), which is the only unambiguous discriminator:
 * plain OCS modes set neither DIPF_IS_ECS nor DIPF_IS_AA, so the property
 * flags cannot separate native from RTG. */
static ULONG find_mode(int w, int h, int depth, const char *monitor,
                       char *name_out, int name_len) {
    ULONG id = INVALID;
    size_t mlen = monitor ? strlen(monitor) : 0;

    while ((id = NextDisplayInfo(id)) != INVALID) {
        struct DimensionInfo dim;
        struct DisplayInfo dinfo;
        struct NameInfo ni;

        /* Skip modes the database itself says cannot be opened. P96 publishes
         * template and coerced entries (DI_P96_MONITOOL, DI_P96_INVALID,
         * DI_P96_COERCED) that match on name and size but fail to open. */
        if (!GetDisplayInfoData(NULL, (UBYTE *)&dinfo, sizeof dinfo, DTAG_DISP, id))
            continue;
        if (dinfo.NotAvailable)
            continue;

        ni.Name[0] = 0;
        if (mlen) {
            if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id))
                continue;
            if (strncmp((const char *)ni.Name, monitor, mlen))
                continue;
        }
        if (GetDisplayInfoData(NULL, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, id)) {
            int mw = dim.Nominal.MaxX - dim.Nominal.MinX + 1;
            int mh = dim.Nominal.MaxY - dim.Nominal.MinY + 1;
            if (mw == w && mh == h && dim.MaxDepth >= depth) {
                if (name_out) {
                    strncpy(name_out, (const char *)ni.Name, name_len - 1);
                    name_out[name_len - 1] = 0;
                }
                return id;
            }
        }
    }
    return INVALID;
}

/* Dump the display database so a usable mode can be picked. */
static void list_modes(void) {
    ULONG id = INVALID;

    printf("%-10s %-28s %5s %5s %5s  flags\n", "id", "name", "w", "h", "depth");
    while ((id = NextDisplayInfo(id)) != INVALID) {
        struct DimensionInfo dim;
        struct DisplayInfo dinfo;
        struct NameInfo ni;
        int mw = 0, mh = 0, md = 0;
        if (!GetDisplayInfoData(NULL, (UBYTE *)&dinfo, sizeof dinfo, DTAG_DISP, id))
            continue;
        if (GetDisplayInfoData(NULL, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, id)) {
            mw = dim.Nominal.MaxX - dim.Nominal.MinX + 1;
            mh = dim.Nominal.MaxY - dim.Nominal.MinY + 1;
            md = dim.MaxDepth;
        }
        /* DTAG_NAME only names base modes, so EHB/HAM/dual-playfield variants
         * come back blank. */
        ni.Name[0] = 0;
        GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id);
        printf("0x%08lx %-28s %5d %5d %5d  0x%08lx%s%s\n", (unsigned long)id,
               ni.Name, mw, mh, md, (unsigned long)dinfo.PropertyFlags,
               (dinfo.PropertyFlags & (DIPF_IS_ECS | DIPF_IS_AA)) ? " chipset" : "",
               dinfo.NotAvailable ? " unavailable" : "");
    }
}

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

/* Directory name for the golden set: goldens are only comparable within one
 * pixel format, so each format gets its own tree.
 *
 * The format is a property of the bitmap, not of its depth: P96 has three
 * 15-bit formats, three 16-bit, two 24-bit and four 32-bit, differing in
 * channel order and byte swapping. So this is indexed by the RGBFormat the
 * bitmap reports, never derived from the depth. */
static const char *format_name(ULONG fmt) {
    static const char *const NAMES[] = {
        "planar",   "clut",     "r8g8b8",   "b8g8r8",   "r5g6b5pc",
        "r5g5b5pc", "a8r8g8b8", "a8b8g8r8", "r8g8b8a8", "b8g8r8a8",
        "r5g6b5",   "r5g5b5",   "b5g6r5pc", "b5g5r5pc", "yuv422cgx",
        "yuv411",   "yuv411pc", "yuv422",   "yuv422pc", "yuv422pa",
        "yuv422papc",
    };
    if (fmt >= (ULONG)(sizeof NAMES / sizeof NAMES[0]))
        return "unknown";
    return NAMES[fmt];
}

static UBYTE *read_pens(struct RastPort *rp, int w, int h, int depth) {
    struct RastPort temprp = *rp;
    UBYTE *idx = AllocVec((ULONG)w * h, MEMF_ANY);
    if (!idx)
        return NULL;

    temprp.Layer = NULL;
    temprp.BitMap = AllocBitMap(w, 1, depth, 0, rp->BitMap);
    if (!temprp.BitMap) {
        FreeVec(idx);
        return NULL;
    }

    ReadPixelArray8(rp, 0, 0, w - 1, h - 1, idx, &temprp);
    FreeBitMap(temprp.BitMap);
    return idx;
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

#define MAX_REPORTED_DIFFS 8

struct RunOpts {
    const char *dir, *golden_dir;
    int w, h;    /* scene: the region rendered and compared */
    int depth, threshold;
    int capture, want_diff;
};

/* Render one testcase and capture or compare it. Returns 1 on failure. */
static int run_test(const struct P96Test *t, struct RastPort *rp,
                    const struct RunOpts *o) {
    UBYTE *idx, *gold;
    int gw, gh, x, y, bad = 0, failed = 0;
    char path[256];

    t->fn(rp, o->w, o->h);
    idx = read_pens(rp, o->w, o->h, o->depth);
    if (!idx) {
        printf("FAIL %-18s memory allocation failed\n", t->name);
        return 1;
    }
    sprintf(path, "%s/%s.png", o->dir, t->name);

    if (o->capture) {
        if (p96cts_write_png(path, idx, o->w, o->h))
            failed = 1;
        else
            printf("captured %s\n", path);
        FreeVec(idx);
        return failed;
    }

    p96cts_write_png(path, idx, o->w, o->h); /* keep the run image beside the golden */
    sprintf(path, "%s/%s.png", o->golden_dir, t->name);
    gold = p96cts_read_png(path, &gw, &gh);
    if (!gold) {
        printf("FAIL %-18s no golden at %s\n", t->name, path);
        FreeVec(idx);
        return 1;
    }

    if (gw != o->w || gh != o->h) {
        printf("FAIL %-18s golden is %dx%d, scene is %dx%d\n", t->name, gw, gh,
               o->w, o->h);
        failed = 1;
    } else {
        for (y = 0; y < o->h; y++)
            for (x = 0; x < o->w; x++)
                if (idx[(ULONG)y * o->w + x] != gold[(ULONG)y * o->w + x])
                    bad++;
        if (bad > o->threshold) {
            printf("FAIL %-18s %d of %d pixels differ\n", t->name, bad,
                   o->w * o->h);
            failed = 1;
        } else {
            printf("PASS %-18s %d pixels differ\n", t->name, bad);
        }
        if (bad) {
            /* Hunting a handful of single pixels in a 320x200 image by eye is
             * hopeless, so name the first few outright. */
            int shown = 0;
            for (y = 0; y < o->h && shown < MAX_REPORTED_DIFFS; y++)
                for (x = 0; x < o->w && shown < MAX_REPORTED_DIFFS; x++) {
                    ULONG p = (ULONG)y * o->w + x;
                    if (idx[p] == gold[p])
                        continue;
                    printf("       at %3d,%3d golden %3d, got %3d\n", x, y,
                           gold[p], idx[p]);
                    shown++;
                }
            if (bad > shown)
                printf("       ... and %d more\n", bad - shown);
        }
        if (o->want_diff && bad) {
            /* Differing pixels in red over the golden scene dimmed to grey:
             * at full intensity the scene buries a few single-pixel diffs. */
            UBYTE *d = AllocVec((ULONG)o->w * o->h, MEMF_CLEAR);
            if (!d) {
                printf("WARNING: failed to allocate diff buffer for %s\n", t->name);
            } else {
                for (y = 0; y < o->h; y++)
                    for (x = 0; x < o->w; x++) {
                        ULONG p = (ULONG)y * o->w + x;
                        d[p] = (idx[p] != gold[p]) ? 2 : (gold[p] ? 5 : 0);
                    }
                sprintf(path, "%s/%s.diff.png", o->dir, t->name);
                p96cts_write_png(path, d, o->w, o->h);
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
        "THRESHOLD/K/N,DIFF/S,LIST/S";
    LONG args[10];
    struct RDArgs *rda;
    struct RunOpts o;
    const char *monitor;
    int failures = 0, rc = 0, g, i;
    int screen_w = 0, screen_h = 0;
    ULONG id;
    struct Screen *scr = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp_off, *rp;
    char golden_buf[64], output_buf[64], mode_name[64];

    printf("%s\n", VERSION_LINE);

    memset(args, 0, sizeof args);
    rda = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)"p96cts");
        return 20;
    }

    memset(&o, 0, sizeof o);
    /* Amiga low-res NTSC. Every primitive is testable at this size, and it
     * keeps the committed goldens small. */
    o.w = 320;
    o.h = 200;
    o.depth = 8;
    o.capture = args[1] != 0;
    monitor = args[2] ? (const char *)args[2] : NULL;
    if (args[6])
        sscanf((const char *)args[6], "%dx%d", &o.w, &o.h);

    /* MODE sizes the screen, SCENE the region actually rendered and compared.
     * They differ because a board need not offer a mode as small as the scene:
     * the smallest Z3660 mode is 640x400, so a 320x200 scene is drawn into the
     * corner of a larger screen and only that corner is compared. Goldens stay
     * small and portable across boards with different mode lists. */
    if (args[5])
        sscanf((const char *)args[5], "%dx%dx%d", &screen_w, &screen_h, &o.depth);
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

    if (args[7])
        o.threshold = *(LONG *)args[7];
    o.want_diff = args[8] != 0;

    if (o.w & 15) {
        printf("width %d must be a multiple of 16\n", o.w);
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
        list_modes();
        goto out;
    }

    mode_name[0] = 0;
    if (monitor) {
        id = find_mode(screen_w, screen_h, o.depth, monitor, mode_name,
                       sizeof mode_name);
        if (id == INVALID) {
            printf("no %s mode %dx%dx%d in the display database\n", monitor,
                   screen_w, screen_h, o.depth);
            failures = 1;
            goto out;
        }
        scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, screen_w, SA_Height,
                             screen_h, SA_Depth, o.depth, SA_Quiet, TRUE,
                             SA_ShowTitle, FALSE, TAG_DONE);
        if (!scr) {
            printf("OpenScreen failed\n");
            failures = 1;
            goto out;
        }
        SetRGB32(&scr->ViewPort, 0, 0, 0, 0);
        SetRGB32(&scr->ViewPort, 1, ~0UL, ~0UL, ~0UL);
        SetRGB32(&scr->ViewPort, 2, ~0UL, 0, 0);
        rp = &scr->RastPort;
    } else {
        /* BMF_USERPRIVATE is documented as fast-memory and never touched by
         * board hardware, so rtg.library rasterises this itself. That is the
         * reference: P96's own software implementation of the same
         * primitives, independent of any card driver and of the blitter. */
        bm = p96AllocBitMap(screen_w, screen_h, o.depth,
                            BMF_CLEAR | BMF_USERPRIVATE, NULL, RGBFB_CLUT);
        if (!bm) {
            printf("p96AllocBitMap %dx%dx%d failed\n", screen_w, screen_h,
                   o.depth);
            failures = 1;
            goto out;
        }
        InitRastPort(&rp_off);
        rp_off.BitMap = bm;
        rp = &rp_off;
    }

    /* Ask the bitmap what it actually is rather than assuming from depth, and
     * refuse formats the readback cannot express: ReadPixelArray8 yields pen
     * values, which only means anything for a palette bitmap. */
    {
        ULONG fmt = p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT);
        if (fmt != RGBFB_CLUT) {
            printf("mode is %s; only clut is supported so far "
                   "(readback is ReadPixelArray8)\n",
                   format_name(fmt));
            failures = 1;
            goto cleanup;
        }
        if (!monitor && p96GetBitMapAttr(rp->BitMap, P96BMA_ISONBOARD)) {
            printf("reference bitmap landed on the board; it would not be "
                   "an independent reference\n");
            failures = 1;
            goto cleanup;
        }
        /* Goldens are per pixel format; run output is per monitor, so several
         * boards can be compared against the one reference set. */
        sprintf(golden_buf, "golden/%s", format_name(fmt));
        sprintf(output_buf, "output/%s", monitor ? monitor : "softrast");
        o.golden_dir = args[4] ? (const char *)args[4] : golden_buf;
        o.dir = args[3] ? (const char *)args[3]
                        : (o.capture ? o.golden_dir : output_buf);
    }

    printf("testing %s, %dx%dx%d, scene %dx%d, %s golden/%s\n",
           monitor ? (mode_name[0] ? mode_name : monitor)
                   : "P96 software rasteriser",
           screen_w, screen_h, o.depth, o.w, o.h,
           o.capture ? "capturing to" : "comparing against",
           format_name(p96GetBitMapAttr(rp->BitMap, P96BMA_RGBFORMAT)));

    make_path(o.dir);

    for (g = 0; g < NGROUPS; g++)
        for (i = 0; i < GROUPS[g]->count; i++) {
            const struct P96Test *t = &GROUPS[g]->tests[i];
            if (selected((STRPTR *)args[0], t->name))
                failures += run_test(t, rp, &o);
        }

cleanup:
    if (scr)
        CloseScreen(scr);
    if (bm)
        p96FreeBitMap(bm);
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
