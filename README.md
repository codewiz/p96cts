# P96CTS -- P96 driver Conformance Test Suite

![P96CTS](logo/p96cts-logo-medium.png)

Validates the rendering primitives of [P96](https://wiki.icomp.de/wiki/P96)
RTG card drivers as well as the graphics.library accelerated rendering
routines.

Each testcase renders a scene and compares it pixel by pixel against a
committed reference in `golden/`. The references are captured from a working
implementation rather than drawn by hand: they come from `rtg.library`, P96's
own software rasterizer, so a driver is checked against what P96 produces for
the same primitives without a board involved.

Runs are non-interactive and the exit code reflects the result, so the suite
works as an automated check -- including under an emulator with no display.

This is a different tool from iComp's
[P96Tests](https://aminet.net/package/dev/src/P96Tests), which is an
interactive visual suite by the P96 maintainer and covers far more of the
driver surface. Use that one to look at a driver; use this one to gate a
change.


## Running

Run all tests for a particular monitor and video mode (WxHxD):

    p96cts Z3660 640x480x8
    p96cts PAL 320x256x8

Output looks like:

    p96cts 0.8 (24.7.2026) by Bernie Innocenti
    testing Z3660 640x400x8 clut, scene 320x200
    PASS DrawLine-solid
    PASS DrawLine-pattern
    FAIL DrawLine-complement      4 of 64000 pixels differ
           at 247, 72 golden  89, got 166
           at  73,128 golden 202, got  53
           ... and 2 more
           captured output/Z3660/320x200x8/DrawLine-complement.fail.png
           wrote difference to output/Z3660/320x200x8/DrawLine-complement.diff.png


Reference images live in `golden/WxHxD/`. A failing test writes two
images to `output/<monitor>/WxHxD/`:
- `<test>.fail.png`, what the run actually rendered, and
- `<test>.diff.png`, the differing pixels in red over the golden dimmed to gray.

To generate all golden images for a particular scene size and depth, run:

    p96cts softrast 320x200x8 CAPTURE


### Arguments

`MONITOR` and `MODE` are positional and both required for a run, so the usual
invocation is `p96cts <monitor> <WxHxD>`.

| Argument | Meaning |
|---|---|
| `MONITOR` | Board to render on; `softrast` for the software rasterizer |
| `MODE` | Screen mode as `WxHxD` |
| `TEST/K` | One testcase as `<group>-<test>`; all of them by default |
| `CAPTURE/S` | Write the reference instead of comparing against it |
| `SCENE/K` | Region rendered and compared, as `WxH` (default `320x200`) |
| `GOLDENDIR/K` | Reference directory (default `golden/<scene>x<depth>`) |
| `OUTDIR/K` | Output directory (default `output/<monitor>/<scene>x<depth>`) |
| `THRESHOLD/K/N` | Tolerate up to this many differing pixels |
| `LISTMODES/S` | Dump the display database and exit |
| `LISTTESTS/S` | List the testcase names `TEST` accepts and exit |
| `HELP/S` | Print this table and exit; `-h` and `--help` work too |



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

    make docker-thirdparty

The archives are reproducible, so a rebuild can be checked byte for byte
against the committed ones. `third_party/README.md` has the upstream versions,
checksums, and why both are built `-noixemul`.


## Adding testcases

A test group is one translation unit in `tests/` exporting a `P96TestGroup`;
see `tests/drawline.c`. Add the file to `OBJS` in the Makefile and the group to
`GROUPS` in `p96cts.c`. A group is named after the function it exercises
(`DrawLine`, `RectFill`, `ClipBlit`, `BltTemplate`, `BltPattern`) and a testcase for what it does (`solid`,
`overlap`); the full name a user types is `<group>-<test>`, matched
case-insensitively. `LISTTESTS` prints them all.

A testcase renders a complete scene, clearing it first, and must keep all
drawing inside the bitmap: the RastPort has no Layer, so graphics.library does
not clip it and drawing outside corrupts memory.

Scenes should be built so that a wrong driver cannot pass by accident. Drawing
solid lines in one pen, for instance, cannot detect a pixel written twice --
it takes a mode like `COMPLEMENT`, where writing twice is not the same as
writing once, and a figure whose lines actually cross.


## Status

Two depths are supported. 8 compares pen values, read back with
`ReadPixelArray8`. 24 compares R8G8B8 read back through `p96ReadPixelArray`,
which converts from whatever the screen's actual format is -- a 24-bit packed
screen and a 32-bit BGRA one produce identical buffers and share one golden
set.

Three scenes are palette only, and permanently: `RectFill-drawmodes`,
`BltTemplate-masks` and `BltPattern-drawmodes` sweep their grids across
`rp->Mask`, which selects bitplanes and has no truecolor counterpart.
Everything else runs at both depths.

A palette run also works on native AGA screens, whose bitmaps are planar rather
than chunky, which puts graphics.library's own rendering up against the same
reference:

    p96cts PAL 320x256x8

15/16-bit modes are the deliberate gap: their reference would have to be
rendered at the same 5-6-5 precision, not just converted to it.

There are no results from real Amiga hardware yet. The reference is P96's own
software rasteriser, so every scene passes there by construction; the columns
below are how much of it each driver reproduces.

Under Amiberry:

| scene | PAL | uaegfx | CyberVision |
|---|---|---|---|
| DrawLine-solid | ✅ | ✅ | ✅ |
| DrawLine-pattern | ✅ | ✅ | ✅ |
| DrawLine-jam2 | ✅ | ✅ | ✅ |
| DrawLine-inversvid | ✅ | ✅ | ✅ |
| DrawLine-complement | ✅ | ✅ | ✅ |
| RectFill-drawmodes | ✅ | ✅ | ✅ |
| RectFill-edges | ✅ | ✅ | ✅ |
| RectFill-invert | ✅ | ✅ | ✅ |
| ClipBlit-overlap | ✅ | ✅ | ✅ |
| ClipBlit-disjoint | ✅ | ✅ | ✅ |
| BltTemplate-offsets | ✅ | ✅ | ✅ |
| BltTemplate-sizes | ✅ | ✅ | ✅ |
| BltTemplate-drawmodes | ✅ | ❌ | ✅ |
| BltTemplate-masks | ✅ | ✅ | ✅ |
| BltPattern-drawmodes | ✅ | ✅ | ✅ |
| BltPattern-mask | ✅ | ✅ | ✅ |
| BltPattern-phase | ✅ | ✅ | ✅ |

Under Copperline:

| scene | Z3660 |
|---|---|
| DrawLine-solid | ✅ |
| DrawLine-pattern | ✅ |
| DrawLine-jam2 | ✅ |
| DrawLine-inversvid | ✅ |
| DrawLine-complement | ❌ |
| RectFill-drawmodes | ✅ |
| RectFill-edges | ✅ |
| RectFill-invert | ✅ |
| ClipBlit-overlap | ✅ |
| ClipBlit-disjoint | ✅ |
| BltTemplate-offsets | ✅ |
| BltTemplate-sizes | ✅ |
| BltTemplate-drawmodes | ✅ |
| BltTemplate-masks | ✅ |
| BltPattern-drawmodes | ❌ |
| BltPattern-mask | ✅ |
| BltPattern-phase | ✅ |


* CyberVision (S3) needs an amiberry build with the S3 line-drawing fix
([amiberry#2211](https://github.com/BlitterStudio/amiberry/issues/2211)); without
it the `DrawLine` scenes fail by a few pixels at the endpoints.
* uaegfx fails `BltTemplate-drawmodes` by applying `INVERSVID` to the
wrong half of the template.
* Copperline's z3660 fails `DrawLine-complement` by four pixels where
its line rasterizer rounds a vertex differently.
* Copperline's z3660 fails `BltPattern-drawmodes` in the two `JAM2 |
COMPLEMENT` modes, where it does not treat `COMPLEMENT` as ignoring the pens
the way the reference does; the `JAM1 | COMPLEMENT` modes pass.
