// SPDX-License-Identifier: 0BSD
//
// The display database, which is graphics.library's own and is asked the same
// questions whether the mode belongs to a board or to native AGA.
//
// Split out of gfx.c, which is left with the drawing helpers and the readback.

#include <proto/graphics.h>
#include <graphics/displayinfo.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "modes.h"

// Constants from Picasso96Develop/PrivateInclude/boardinfo.h.
//
// Why P96 marked a mode NotAvailable.
//
// MONITOOL is the one worth recognizing: P96 publishes a template entry per
// pixel format for the mode prefs editor to enumerate ("Z36600-P96Mode 8bit"
// and friends, at a nominal 320x200). They are not real modes and never open.
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

// Exact, not >=: a 15-bit request on P96's 16-bit entry opens a 16-bit
// screen. Depth 24 also takes 32, which names the same screens.
static bool depth_matches(int max_depth, int depth) {
    if (depth == 24)
        return max_depth >= 24;
    return max_depth == depth;
}

// Find a display id of the given size/depth, or INVALID_MODE. A width of 0
// matches any size, for when only the depth matters. When name_out is given,
// the matched mode's name is copied into it.
//
// `monitor` selects by mode-name prefix ("PAL", "Z3660", ...) and NULL matches
// any. That prefix is the only unambiguous discriminator: plain OCS modes set
// neither DIPF_IS_ECS nor DIPF_IS_AA, so the property flags cannot separate
// native from RTG.
ULONG find_mode(int w, int h, int depth, const char *monitor, char *name_out,
                int name_len) {
    ULONG id = INVALID_MODE;
    size_t mlen = monitor ? strlen(monitor) : 0;

    while ((id = NextDisplayInfo(id)) != INVALID_MODE) {
        struct DisplayInfo dinfo;
        struct NameInfo ni;
        struct DimensionInfo dim;

        // Skip modes the database itself says cannot be opened. P96 publishes
        // entries that match on name and size but fail to open -- the DI_P96_*
        // reasons above. Without this test one of them wins the search and
        // OpenScreen then fails.
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
            if ((w <= 0 || (mw == w && mh == h)) &&
                depth_matches(dim.MaxDepth, depth)) {
                if (name_out) {
                    strncpy(name_out, (const char *)ni.Name, name_len - 1);
                    name_out[name_len - 1] = 0;
                }
                return id;
            }
        }
    }
    return INVALID_MODE;
}

// The smallest available mode (by area, then width) serving `depth` whose
// nominal size contains min_w x min_h, or INVALID_MODE. Monitor selection is
// find_mode()'s name prefix; the winning mode's size lands in *out_w/*out_h.
//
// Not BestModeIDA(), for two guarantees it does not give. Its BIDTAG_Depth
// is documented as "minimum the returned ModeID must support", the >=
// match depth_matches() exists to avoid. And BIDTAG_NominalWidth/Height
// "together make the aspect ratio", with DesiredWidth/Height only breaking
// ties, so nothing promises a mode large enough to contain the scene --
// the result could be a scrolling screen. (Its monitor selection would be
// fine: BIDTAG_MonitorID, with the id resolved from a matching mode.)
ULONG pick_mode(int min_w, int min_h, int depth, const char *monitor,
                int *out_w, int *out_h) {
    ULONG id = INVALID_MODE, best = INVALID_MODE;
    LONG best_area = 0;
    size_t mlen = monitor ? strlen(monitor) : 0;

    while ((id = NextDisplayInfo(id)) != INVALID_MODE) {
        struct DisplayInfo dinfo;
        struct NameInfo ni;
        struct DimensionInfo dim;

        if (!GetDisplayInfoData(NULL, (UBYTE *)&dinfo, sizeof dinfo, DTAG_DISP, id))
            continue;
        if (dinfo.NotAvailable)
            continue;
        if (mlen) {
            if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id))
                continue;
            if (strncmp((const char *)ni.Name, monitor, mlen))
                continue;
        }
        if (!GetDisplayInfoData(NULL, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, id))
            continue;

        int mw = dim.Nominal.MaxX - dim.Nominal.MinX + 1;
        int mh = dim.Nominal.MaxY - dim.Nominal.MinY + 1;
        LONG area = (LONG)mw * mh;

        if (!depth_matches(dim.MaxDepth, depth) || mw < min_w || mh < min_h)
            continue;
        if (best == INVALID_MODE || area < best_area ||
            (area == best_area && mw < *out_w)) {
            best = id;
            best_area = area;
            *out_w = mw;
            *out_h = mh;
        }
    }
    return best;
}

// Dump the display database to stdout so a usable mode can be picked.
void list_modes(void) {
    ULONG id = INVALID_MODE;

    printf("%-10s %-28s %-14s Flags\n", "ID", "Name", "Mode");
    while ((id = NextDisplayInfo(id)) != INVALID_MODE) {
        struct DisplayInfo dinfo;
        struct DimensionInfo dim;
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
        // DTAG_NAME only names base modes, so EHB/HAM/dual-playfield variants
        // come back blank.
        ni.Name[0] = 0;
        GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id);
        snprintf(mode, sizeof mode, "%dx%dx%d", mw, mh, md);
        printf("0x%08lx %-28s %-14s 0x%08lx%s\n", (unsigned long)id, ni.Name,
               mode, (unsigned long)dinfo.PropertyFlags,
               dinfo.NotAvailable ? unavailable_reason(dinfo.NotAvailable) : "");
    }
}
