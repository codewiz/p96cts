#!/bin/bash
# Build zlib and libpng for m68k-amigaos without ixemul and install them into
# third_party/. Run from the repository root inside the amiga-gcc container:
#
#   docker run --rm --user $(id -u):$(id -u) -v .:/src -w /src \
#       stefanreinauer/amiga-gcc:gcc-v16.1 bash third_party/build.sh
#
# Expects the upstream tarballs already unpacked under tmp/. See
# third_party/README.md for the URLs and checksums.

# bash, not sh: -E and pipefail are not POSIX. The container's dash happens to
# accept them, but an older dash would not, so ask for bash explicitly.
set -Eeuxo pipefail

CC=${CC:-m68k-amigaos-gcc}
AR=${AR:-m68k-amigaos-ar}
RANLIB=${RANLIB:-m68k-amigaos-ranlib}

SRC=tmp
OUT=third_party
CFLAGS="-noixemul -O2 -fomit-frame-pointer"

rm -rf "$OUT/zlib" "$OUT/libpng" "$SRC/build-z" "$SRC/build-png"
mkdir -p "$OUT/zlib/include" "$OUT/zlib/lib" "$SRC/build-z"
mkdir -p "$OUT/libpng/include" "$OUT/libpng/lib" "$SRC/build-png"

# ---- zlib ----------------------------------------------------------------
# gz*.c is omitted deliberately: libpng never uses the gzFile API, and those
# files are what pull stdio and errno into the link -- the exact coupling that
# made the prebuilt ixemul libpng unusable here.
Z=$SRC/zlib-1.3.1
ZSRC="adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil"
for f in $ZSRC; do
	$CC $CFLAGS -I"$Z" -c -o "$SRC/build-z/$f.o" "$Z/$f.c"
done
$AR rcsD "$OUT/zlib/lib/libz.a" $SRC/build-z/*.o
$RANLIB "$OUT/zlib/lib/libz.a"
cp "$Z/zlib.h" "$Z/zconf.h" "$OUT/zlib/include/"
cp "$Z/LICENSE" "$OUT/zlib/"

# ---- libpng --------------------------------------------------------------
# scripts/pnglibconf.h.prebuilt is the stock upstream configuration. Using it
# avoids libpng's configure, which does not cross-compile cleanly here.
P=$SRC/libpng-1.6.55
cp "$P/scripts/pnglibconf.h.prebuilt" "$SRC/build-png/pnglibconf.h"
PSRC="png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil \
      pngset pngtrans pngwio pngwrite pngwtran pngwutil"
for f in $PSRC; do
	$CC $CFLAGS -I"$SRC/build-png" -I"$P" -I"$OUT/zlib/include" \
	    -c -o "$SRC/build-png/$f.o" "$P/$f.c"
done
$AR rcsD "$OUT/libpng/lib/libpng16.a" $SRC/build-png/*.o
$RANLIB "$OUT/libpng/lib/libpng16.a"
cp "$P/png.h" "$P/pngconf.h" "$SRC/build-png/pnglibconf.h" "$OUT/libpng/include/"
cp "$P/LICENSE" "$OUT/libpng/"

echo "built:"
ls -l "$OUT/zlib/lib" "$OUT/libpng/lib"
