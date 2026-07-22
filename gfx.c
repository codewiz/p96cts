/* SPDX-License-Identifier: 0BSD */

/* Everything that talks to graphics.library and P96 on the harness's behalf:
 * the color helpers testcases draw through, the display-database search that
 * turns a WxHxD request into a display id, and the readback that turns a
 * rendered scene into the bytes the comparison works on.
 *
 * Split out of p96cts.c, which is left with argument parsing and the run
 * loop.
 */

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"

/* --- colors --------------------------------------------------------------- */

int p96cts_truecolor;

/* JAM1-fill a rectangle in the given color: a pen number on a palette screen,
 * 0x00RRGGBB on truecolor. On a palette screen the corners go to RectFill as
 * given, including deliberately swapped ones; the truecolor path sorts them
 * first, since p96RectFill's contract requires min <= max.
 *
 * Deliberately not inline: the body is two library calls, which dwarf the call
 * overhead, and inlining it would drag proto/graphics.h and proto/Picasso96.h
 * into every file that includes gfx.h. */
void p96cts_fill(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                 ULONG color) {
    if (p96cts_truecolor) {
        SHORT t;
        if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
        if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
        p96RectFill(rp, x1, y1, x2, y2, color);
        return;
    }
    SetDrMd(rp, JAM1);
    SetAPen(rp, color);
    RectFill(rp, x1, y1, x2, y2);
}

/* Fill the whole scene with one color, as above. 0 is black either way. */
void p96cts_clear(struct RastPort *rp, SHORT w, SHORT h, ULONG color) {
    p96cts_fill(rp, 0, 0, w - 1, h - 1, color);
}

/* --- display database ----------------------------------------------------- */

#define INVALID P96CTS_INVALID_MODE

/* Why P96 marked a mode NotAvailable. From P96's boardinfo.h, which lives in
 * PrivateInclude and is not shipped with the toolchain, so the values are
 * repeated rather than included.
 *
 * MONITOOL is the one worth recognizing: P96 publishes a template entry per
 * pixel format for the mode prefs editor to enumerate ("Z36600-P96Mode 8bit"
 * and friends, at a nominal 320x200). They are not real modes and never
 * open. */
#define DI_P96_INVALID 0x1000
#define DI_P96_MONITOOL 0x2000
#define DI_P96_COERCED 0x4000

static const char *unavailable_reason(UWORD na) {
    if (na & DI_P96_MONITOOL)
        return " template";
    if (na & DI_P96_COERCED)
        return " coerced";
    if (na & DI_P96_INVALID)
        return " invalid";
    return " unavailable";
}

/* Find a display id of the given size/depth, or P96CTS_INVALID_MODE. A width
 * of 0 matches any size, for when only the depth matters. When name_out is
 * given, the matched mode's name is copied into it.
 *
 * `monitor` selects by mode-name prefix ("PAL", "Z3660", ...) and NULL matches
 * any. That prefix is the only unambiguous discriminator: plain OCS modes set
 * neither DIPF_IS_ECS nor DIPF_IS_AA, so the property flags cannot separate
 * native from RTG. */
ULONG p96cts_find_mode(int w, int h, int depth, const char *monitor,
                       char *name_out, int name_len) {
    ULONG id = INVALID;
    size_t mlen = monitor ? strlen(monitor) : 0;

    while ((id = NextDisplayInfo(id)) != INVALID) {
        struct DimensionInfo dim;
        struct DisplayInfo dinfo;
        struct NameInfo ni;

        /* Skip modes the database itself says cannot be opened. P96 publishes
         * entries that match on name and size but fail to open -- the
         * DI_P96_* reasons above. Without this test one of them wins the
         * search and OpenScreen then fails. */
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
            if ((w <= 0 || (mw == w && mh == h)) && dim.MaxDepth >= depth) {
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

/* Dump the display database to stdout so a usable mode can be picked. */
void p96cts_list_modes(void) {
    ULONG id = INVALID;

    printf("%-10s %-28s %-14s flags\n", "id", "name", "mode");
    while ((id = NextDisplayInfo(id)) != INVALID) {
        struct DimensionInfo dim;
        struct DisplayInfo dinfo;
        struct NameInfo ni;
        char mode[24];
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
        snprintf(mode, sizeof mode, "%dx%dx%d", mw, mh, md);
        printf("0x%08lx %-28s %-14s 0x%08lx%s\n", (unsigned long)id, ni.Name,
               mode, (unsigned long)dinfo.PropertyFlags,
               dinfo.NotAvailable ? unavailable_reason(dinfo.NotAvailable) : "");
    }
}

/* --- readback ------------------------------------------------------------- */

/* Reported so a run says what it actually rendered on. The format is a
 * property of the bitmap, not of its depth: P96 has three 15-bit formats,
 * three 16-bit, two 24-bit and four 32-bit, differing in channel order and
 * byte swapping. So this is indexed by the RGBFormat the bitmap reports,
 * never derived from the depth. */
const char *p96cts_format_name(ULONG fmt) {
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

/* Read the scene back as one pen per pixel, into a freshly AllocVec'd buffer
 * the caller FreeVec's, or NULL. Needs a bitmap addressed by pen.
 *
 * ReadPixelArray8 works in 16-pixel granules, so w must be a multiple of 16;
 * that is where the screen-width constraint on a run comes from. */
UBYTE *p96cts_read_pens(struct RastPort *rp, SHORT w, SHORT h, int depth) {
    struct RastPort temprp = *rp;
    UBYTE *idx;

    idx = AllocVec((ULONG)w * h, MEMF_ANY);
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

/* The same, three bytes per pixel: the scene read back as R8G8B8 whatever the
 * screen's own format is.
 *
 * p96ReadPixelArray converts into the RenderInfo's format, so a BGRA screen
 * and an RGB one produce identical buffers and share one golden set. */
UBYTE *p96cts_read_rgb(struct RastPort *rp, SHORT w, SHORT h) {
    UBYTE *px = AllocVec((ULONG)w * h * 3, MEMF_ANY);
    if (!px)
        return NULL;

    struct RenderInfo ri;
    ri.Memory = px;
    ri.BytesPerRow = w * 3;
    ri.pad = 0;
    ri.RGBFormat = RGBFB_R8G8B8;
    p96ReadPixelArray(&ri, 0, 0, rp, 0, 0, w, h);
    return px;
}
