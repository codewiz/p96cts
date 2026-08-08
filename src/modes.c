// SPDX-License-Identifier: 0BSD
//
// The display database, which is graphics.library's own and is asked the same
// questions whether the mode belongs to a board or to native AGA.
//
// Split out of gfx.c, which is left with the drawing helpers and the readback.

#include <proto/graphics.h>
#include <graphics/displayinfo.h>
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
            if ((w <= 0 || (mw == w && mh == h)) && dim.MaxDepth >= depth) {
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

// Dump the display database to stdout so a usable mode can be picked.
void list_modes(void) {
    ULONG id = INVALID_MODE;

    printf("%-10s %-28s %-14s flags\n", "id", "name", "mode");
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
