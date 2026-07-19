# P96CTS -- P96 driver Conformance Test Suite

Validates the rendering primitives of [P96](https://wiki.icomp.de/wiki/P96)
RTG card drivers as well as the graphics.library accelerated rendering
routines.

Each testcase renders a scene and compares it pixel by pixel against a
committed reference in `golden/`. The references are captured from a working
implementation rather than drawn by hand: `golden/clut` comes from AGA
chipset modes, so a driver is checked against what the Amiga's own graphics
hardware produces for the same primitives.

Runs are non-interactive and the exit code reflects the result, so the suite
works as an automated check -- including under an emulator with no display.

This is a different tool from iComp's
[P96Tests](https://aminet.net/package/dev/src/P96Tests), which is an
interactive visual suite by the P96 maintainer and covers far more of the
driver surface. Use that one to look at a driver; use this one to gate a
change.


## Running

Capture the reference from P96's software rasteriser, then compare a board
against it:

    p96cts CAPTURE
    p96cts MONITOR=Z3660 MODE=640x400x8 DIFF

Output looks like:

    p96cts 0.1 (19.7.2026)
    testing Z3660:640x400  8bit, 640x400x8, scene 320x200, comparing against golden/clut
    PASS lines-solid        0 pixels differ
    PASS lines-pattern      0 pixels differ
    FAIL lines-complement   4 of 64000 pixels differ
           at 247, 72 golden  89, got 166

Reference images live in `golden/<pixel format>/`, a run's own images in
`output/<monitor>/`, and `DIFF` additionally writes `<test>.diff.png` marking
the differing pixels in red. All images are 8-bit palette PNGs, which compress
these flat synthetic scenes to a few hundred bytes and can be viewed anywhere,
so the references are committed rather than regenerated.


### Arguments

| Argument | Meaning |
|---|---|
| `TEST/M` | Testcases to run; all of them by default |
| `CAPTURE/S` | Write the reference instead of comparing against it |
| `MONITOR/K` | Render on a screen of this monitor; omit to use the software rasteriser |
| `MODE/K` | Screen mode as `WxHxD` (default: the scene size) |
| `SCENE/K` | Region rendered and compared, as `WxH` (default `320x200`) |
| `GOLDEN/K` | Reference directory (default `golden/<format>`) |
| `DIR/K` | Output directory (default `output/<monitor>`) |
| `THRESHOLD/K/N` | Tolerate up to this many differing pixels |
| `DIFF/S` | Write a diff image on mismatch |
| `LIST/S` | Dump the display database and exit |

`MODE` and `SCENE` are separate because a board need not offer a mode as small
as the scene. The smallest Z3660 mode is 640x400, so a 320x200 scene is drawn
into the corner of a larger screen and only that corner is compared, which
keeps reference images small and comparable across boards with different mode
lists.


## Building

The default include path is where the amiga-gcc toolchain ships the P96
headers, so a containerised build takes no arguments:

    make docker-build

With a toolchain that does not bundle them, point at an unpacked
`P96Develop.lha`:

    make CC=/path/to/bin/m68k-amigaos-gcc \
         P96INC=/path/to/Picasso96Develop/Include

Images are read and written with zlib and libpng, which are committed under
`third_party/` already built for this target, so nothing needs fetching or
cross-building first. They rarely need rebuilding, but when they do, the same
container runs their build script:

    docker run --rm --user $(id -u):$(id -g) -v .:/src -w /src \
        stefanreinauer/amiga-gcc:gcc-v16.1 bash third_party/build.sh

The archives are reproducible, so a rebuild can be checked byte for byte
against the committed ones. `third_party/README.md` has the upstream versions,
checksums, and why both are built `-noixemul`.


## Adding testcases

A test group is one translation unit exporting a `P96TestGroup`; see
`drawline.c`. Add the file to `OBJS` in the Makefile and the group to `GROUPS`
in `p96cts.c`.

A testcase renders a complete scene, clearing it first, and must keep all
drawing inside the bitmap: the RastPort has no Layer, so graphics.library does
not clip it and drawing outside corrupts memory.

Scenes should be built so that a wrong driver cannot pass by accident. Drawing
solid lines in one pen, for instance, cannot detect a pixel written twice --
it takes a mode like `COMPLEMENT`, where writing twice is not the same as
writing once, and a figure whose lines actually cross.


## Status

Only 8-bit palette (`clut`) is supported so far: results are read back with
`ReadPixelArray8`, which yields pen values. Deeper formats need
`p96ReadPixelArray` and a wider comparison path.


## Licence

0BSD, the same terms iComp chose for P96Tests, so testcases can move freely
between the two and either can be absorbed into a driver tree regardless of
its own licence. See `LICENSE`.
