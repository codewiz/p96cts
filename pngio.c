/* SPDX-License-Identifier: 0BSD */

/* PNG reading and writing.
 *
 * Palette runs use 8-bit palette PNGs so pen values survive the round trip
 * untouched; truecolor runs use 8-bit RGB. Either way the file holds exactly
 * the compared bytes. libpng is driven through dos.library handles rather
 * than stdio, matching the rest of p96cts.
 */

#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <png.h>
#include <limits.h>
#include <stdio.h>

#include "pngio.h"

/* --- palette --------------------------------------------------------------
 *
 * Pens 0-4 are named colors and pen 5 is a dim gray, with the rest a 3-3-2
 * RGB cube. A gray ramp would be useless: COMPLEMENT turns pen 1 into pen 254
 * and pen 0 into pen 255, which as grays are indistinguishable from the white
 * they inverted, making the scene look blank. Under 3-3-2 they come out as
 * clearly different colors.
 */
static void build_palette(png_color *pal) {
    int i;

    for (i = 0; i < 256; i++) {
        pal[i].red = (png_byte)((i & 7) * 36);
        pal[i].green = (png_byte)(((i >> 3) & 7) * 36);
        pal[i].blue = (png_byte)(((i >> 6) & 3) * 85);
    }
    pal[0].red = pal[0].green = pal[0].blue = 0;       /* black */
    pal[1].red = pal[1].green = pal[1].blue = 255;     /* white */
    pal[2].red = 255; pal[2].green = 0;   pal[2].blue = 0;   /* red   */
    pal[3].red = 0;   pal[3].green = 255; pal[3].blue = 0;   /* green */
    pal[4].red = 0;   pal[4].green = 0;   pal[4].blue = 255; /* blue  */
    pal[5].red = pal[5].green = pal[5].blue = 64;      /* diff context */
}

/* --- dos.library I/O for libpng ------------------------------------------- */

static void dos_write(png_structp png, png_bytep data, size_t length) {
    BPTR f = (BPTR)png_get_io_ptr(png);

    if (Write(f, data, (LONG)length) != (LONG)length)
        png_error(png, "short write");
}

static void dos_flush(png_structp png) { (void)png; }

static void dos_read(png_structp png, png_bytep data, size_t length) {
    BPTR f = (BPTR)png_get_io_ptr(png);

    if (Read(f, data, (LONG)length) != (LONG)length)
        png_error(png, "short read");
}

/* --- write ---------------------------------------------------------------- */

int p96cts_write_png(const char *path, const UBYTE *px, SHORT w, SHORT h,
                     int bpp) {
    png_structp png = NULL;
    png_infop info = NULL;
    png_color pal[256];

    BPTR f = Open((STRPTR)path, MODE_NEWFILE);
    if (!f) {
        printf("cannot create %s\n", path);
        return 1;
    }

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png)
        info = png_create_info_struct(png);
    /* libpng reports errors by longjmp-ing back here. */
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        printf("cannot encode %s\n", path);
        if (png)
            png_destroy_write_struct(&png, info ? &info : NULL);
        Close(f);
        return 1;
    }

    png_set_write_fn(png, (png_voidp)f, dos_write, dos_flush);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 bpp == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    if (bpp != 3) {
        build_palette(pal);
        png_set_PLTE(png, info, pal, 256);
    }
    png_write_info(png, info);
    for (int y = 0; y < h; y++)
        png_write_row(png, (png_bytep)(px + (ULONG)y * w * bpp));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    Close(f);
    return 0;
}

/* --- read ----------------------------------------------------------------- */

UBYTE *p96cts_read_png(const char *path, SHORT *w, SHORT *h, int bpp) {
    png_structp png = NULL;
    png_infop info = NULL;
    /* volatile: written after setjmp and freed on the error path, so a
     * longjmp must not leave it holding a stale register copy. */
    UBYTE *volatile idx = NULL;
    png_uint_32 pw, ph;
    int depth, color;

    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    if (!f)
        return NULL;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png)
        info = png_create_info_struct(png);
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        printf("cannot decode %s\n", path);
        if (idx)
            FreeVec(idx);
        if (png)
            png_destroy_read_struct(&png, info ? &info : NULL, NULL);
        Close(f);
        return NULL;
    }

    png_set_read_fn(png, (png_voidp)f, dos_read);
    png_read_info(png, info);
    pw = png_get_image_width(png, info);
    ph = png_get_image_height(png, info);
    depth = png_get_bit_depth(png, info);
    color = png_get_color_type(png, info);

    /* Anything else would have to be converted, and a converted reference is
     * no longer a reference: the comparison is on the stored bytes. */
    if (depth != 8 ||
        color != (bpp == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_PALETTE)) {
        printf("%s is not an 8-bit %s PNG\n", path,
               bpp == 3 ? "RGB" : "palette");
        png_destroy_read_struct(&png, &info, NULL);
        Close(f);
        return NULL;
    }
    if (!pw || !ph || pw > (png_uint_32)SHRT_MAX || ph > (png_uint_32)SHRT_MAX)
        png_error(png, "image dimensions are too large");

    idx = AllocVec((ULONG)pw * ph * bpp, MEMF_ANY);
    if (!idx)
        png_error(png, "out of memory");
    for (SHORT y = 0; y < (SHORT)ph; y++)
        png_read_row(png, (png_bytep)(idx + (ULONG)y * pw * bpp), NULL);
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    Close(f);

    *w = (SHORT)pw;
    *h = (SHORT)ph;
    return idx;
}
