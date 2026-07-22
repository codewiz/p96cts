// SPDX-License-Identifier: 0BSD
//
// PNG reading and writing.
//
// Palette runs use 8-bit palette PNGs so pen values survive the round trip
// untouched; truecolor runs use 8-bit RGB. Either way the file holds exactly
// the compared bytes. libpng is driven through dos.library handles rather
// than stdio, matching the rest of p96cts.

#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <png.h>
#include <limits.h>
#include <stdio.h>

#include "palette.h"
#include "pngio.h"

// The PNG carries the palette the screen was opened with, taken straight from
// the LoadRGB32 table: a header word pair, then three 32-bit guns per pen, of
// which the high byte is the value.
static void build_palette(png_color *pal) {
    const ULONG *table = p96cts_palette() + 1;

    for (int i = 0; i < P96CTS_PALETTE_ENTRIES; i++) {
        pal[i].red = (png_byte)(table[i * 3] >> 24);
        pal[i].green = (png_byte)(table[i * 3 + 1] >> 24);
        pal[i].blue = (png_byte)(table[i * 3 + 2] >> 24);
    }
}

// --- dos.library I/O for libpng ---------------------------------------------

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

// --- write ------------------------------------------------------------------

int p96cts_write_png(const char *path, const UBYTE *px, SHORT w, SHORT h,
                     int bpp) {
    png_structp png = NULL;
    png_infop info = NULL;
    // static, not automatic: 768 bytes is a sixth of the 4K stack a Shell
    // gives a command by default, and this is called from inside run_test.
    static png_color pal[P96CTS_PALETTE_ENTRIES];

    BPTR f = Open((STRPTR)path, MODE_NEWFILE);
    if (!f) {
        printf("cannot create %s\n", path);
        return 1;
    }

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png)
        info = png_create_info_struct(png);
    // libpng reports errors by longjmp-ing back here.
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
        png_set_PLTE(png, info, pal, P96CTS_PALETTE_ENTRIES);
    }
    png_write_info(png, info);
    for (int y = 0; y < h; y++)
        png_write_row(png, (png_bytep)(px + (ULONG)y * w * bpp));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    Close(f);
    return 0;
}

// --- read -------------------------------------------------------------------

UBYTE *p96cts_read_png(const char *path, SHORT *w, SHORT *h, int bpp) {
    png_structp png = NULL;
    png_infop info = NULL;
    // volatile: written after setjmp and freed on the error path, so a
    // longjmp must not leave it holding a stale register copy.
    UBYTE *volatile idx = NULL;

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
    png_uint_32 pw = png_get_image_width(png, info);
    png_uint_32 ph = png_get_image_height(png, info);
    int depth = png_get_bit_depth(png, info);
    int color = png_get_color_type(png, info);

    // Anything else would have to be converted, and a converted reference is
    // no longer a reference: the comparison is on the stored bytes.
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
