# third_party

Upstream libraries p96cts links against, built for m68k-amigaos and committed
as static archives. Each library keeps its own `LICENSE` beside its own files;
those terms govern the contents of that subdirectory, not p96cts's 0BSD.

| library | version | upstream tarball sha256 |
|---|---|---|
| zlib | 1.3.1 | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` |
| libpng | 1.6.55 | `4b0abab6d219e95690ebe4db7fc9aa95f4006c83baaa022373c0c8442271283d` |

Sources: <https://zlib.net/fossils/zlib-1.3.1.tar.gz> and
<https://download.sourceforge.net/libpng/libpng-1.6.55.tar.gz>.

## Why the binaries are committed

p96cts is a small test tool for a cross-compiled target. Requiring everyone who
builds it to first cross-build libpng would cost far more than the 260 KB these
archives take. `build.sh` plus the checksums above is what keeps that honest:
the archives are reproducible, so anyone can rebuild and compare.

Both were built with `m68k-amigaos-gcc (GCC) 16.1.1b 20260516` and are
byte-identical across rebuilds (`ar rcsD`, no timestamps). To reproduce, unpack
both tarballs into `tmp/` and run, from the repository root:

    docker run --rm --user $(id -u):$(id -g) -v .:/src -w /src \
        stefanreinauer/amiga-gcc:gcc-v16.1 bash third_party/build.sh

    sha256sum third_party/*/lib/*.a
    1449a800f879690fc3a246764c42dbd35f3917ef0feea5624e4842a45e8d3cf9  third_party/zlib/lib/libz.a
    0701cfdf2cb7703b207bbe1c4fec28964eacf38cc8c18e780e4bfca77fad8ef9  third_party/libpng/lib/libpng16.a

## Build notes

Both libraries are built `-noixemul`, against libnix. This is not optional: the
prebuilt m68k libpng available elsewhere is an ixemul build, and ixemul makes
`errno` a plain global while libnix defines it as `#define errno (*__errno)`.
An ixemul-built archive therefore carries an unresolvable reference to `errno`,
and shimming it would splice two incompatible C runtimes -- `FILE` layouts
included -- into one program. Building from source is the only clean fix.

Two deliberate deviations from a stock build:

- **zlib omits `gz*.c`.** libpng never uses the `gzFile` API, and those files
  are what pull stdio and errno into the link.
- **libpng uses `scripts/pnglibconf.h.prebuilt`**, the stock upstream
  configuration, rather than running `configure`, which does not cross-compile
  cleanly for this target.

The result is a plain 68000 build with software floating point, so it runs on
any Amiga rather than requiring an 020+ or an FPU.
