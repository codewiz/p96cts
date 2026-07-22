/* SPDX-License-Identifier: 0BSD */

/* The screen palette, and the same colors for the PNG writer.
 *
 * Every pen is given a color, because on a truecolor screen a golden records
 * the colors a scene's pens resolved to. Intuition only initializes pens 0-3
 * and 17-19, from the user's Preferences, and leaves the rest to
 * graphics.library's own defaults, so anything not set here would make a
 * capture depend on the machine it was taken on.
 */

#include "palette.h"

/* A 3-3-2 RGB cube, with the first few pens overridden to colors worth naming.
 * Pen 0 has to be black: p96cts_clear(rp, w, h, 0) paints RGB 0 on truecolor,
 * and the two must agree.
 *
 * Two things a scene must not be built on. Pens 4 and 192 are both pure blue,
 * so they cannot tell each other apart. And the grays -- pen 1 among them --
 * have r == b, so a driver that swaps red and blue renders exactly the color
 * expected of them; the fillrect and copyrect pens are not grays and do catch
 * that. Either trap is silent, since the scene simply compares equal. */
static ULONG pen_rgb(int pen) {
    switch (pen) {
    case 0: return 0x000000; /* black, and the scene background */
    case 1: return 0xFFFFFF; /* white */
    case 2: return 0xFF0000; /* red, also the diff image's marker */
    case 3: return 0x00FF00; /* green */
    case 4: return 0x0000FF; /* blue */
    case 5: return 0x404040; /* dim gray, the diff image's context */
    }
    return ((ULONG)((pen & 7) * 36) << 16) |
           ((ULONG)(((pen >> 3) & 7) * 36) << 8) |
           (ULONG)(((pen >> 6) & 3) * 85);
}

/* The palette as a LoadRGB32 table: a header word pair, then three 32-bit guns
 * per pen, terminated by a zero. Suitable for SA_Colors32 at OpenScreen, and
 * the PNG writer reads the high byte of each gun back out of it. */
const ULONG *p96cts_palette(void) {
    static ULONG table[2 + P96CTS_PALETTE_ENTRIES * 3];
    ULONG *e = table;
    int p;

    *e++ = ((ULONG)P96CTS_PALETTE_ENTRIES << 16) | 0;
    for (p = 0; p < P96CTS_PALETTE_ENTRIES; p++) {
        ULONG rgb = pen_rgb(p);
        /* LoadRGB32 takes 32 bits per gun, so each byte is replicated. */
        *e++ = 0x01010101UL * ((rgb >> 16) & 0xFF);
        *e++ = 0x01010101UL * ((rgb >> 8) & 0xFF);
        *e++ = 0x01010101UL * (rgb & 0xFF);
    }
    *e = 0;
    return table;
}
