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

It also runs on AROS, which has no `Picasso96API.library`: the same questions
go to `cybergraphics.library` and `graphics.library` instead, chosen once at
startup. The references stay the ones captured from P96, so an AROS run is
still checked against what P96 produces.

This is a different tool from iComp's
[P96Tests](https://aminet.net/package/dev/src/P96Tests), which is an
interactive visual suite by the P96 maintainer and covers far more of the
driver surface. Use that one to look at a driver; use this one to gate a
change.


## Running

Run all tests for a particular monitor and video mode (WxHxD):

    p96cts Z3660 640x480x8

Output looks like:

    p96cts 0.11 (29.7.2026) by Bernie Innocenti
    testing Z3660 640x480x8 clut, scene 320x200
    PASS DrawLine-solid
    PASS DrawLine-pattern
    FAIL DrawLine-complement      4 of 64000 pixels differ
           at 247, 72 golden  89, got 166
           at  73,128 golden 202, got  53
           ... and 2 more
           captured output/Z3660/320x200x8/DrawLine-complement.fail.png
           wrote difference to output/Z3660/320x200x8/DrawLine-complement.diff.png

A palette run also works on native AGA screens, whose bitmaps are planar rather
than chunky, which puts graphics.library's own rendering up against the same
reference:

    p96cts PAL 320x256x8


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


## Test Results

The tables below are from emulators. The suite has also been run on a physical
ZZ9000, where everything passed but `BltBitMap-minterms`
([zz9000-drivers#57](https://github.com/BlitterStudio/zz9000-drivers/issues/57));
the fix for that has landed in the board's firmware but has not been rerun on
hardware yet. If you have access to an Amiga with an RTG board, please run the
suite and open an issue to share your results.

### Amiberry

| scene | PAL | uaegfx | CyberVision | ZZ9000 |
|---|---|---|---|---|
| DrawLine-solid | ✅ | ✅ | ✅ | ✅ |
| DrawLine-pattern | ✅ | ✅ | ✅ | ✅ |
| DrawLine-jam2 | ✅ | ✅ | ✅ | ✅ |
| DrawLine-inversvid | ✅ | ✅ | ✅ | ✅ |
| DrawLine-complement | ✅ | ✅ | ✅ | ✅ |
| RectFill-drawmodes | ✅ | ✅ | ✅ | ✅ |
| RectFill-edges | ✅ | ✅ | ✅ | ✅ |
| RectFill-invert | ✅ | ✅ | ✅ | ✅ |
| ClipBlit-overlap | ✅ | ✅ | ✅ | ✅ |
| ClipBlit-disjoint | ✅ | ✅ | ✅ | ✅ |
| BltTemplate-offsets | ✅ | ✅ | ✅ | ✅ |
| BltTemplate-sizes | ✅ | ✅ | ✅ | ✅ |
| BltTemplate-drawmodes | ✅ | ✅| ✅ | ✅ |
| BltTemplate-masks | ✅ | ✅ | ✅ | ✅ |
| BltPattern-drawmodes | ✅ | ✅ | ✅ | ✅ |
| BltPattern-mask | ✅ | ✅ | ✅ | ✅ |
| BltPattern-phase | ✅ | ✅ | ✅ | ✅ |
| BltBitMap-minterms | ✅ | ✅ | ✅ | ✅ |
| BltBitMap-offsets | ✅ | ✅ | ✅ | ✅ |
| BltBitMap-sizes | ✅ | ✅ | ✅ | ✅ |
| BltBitMap-planemask | ✅ | [❌](https://github.com/BlitterStudio/amiberry/issues/2235) | ✅ | ✅ |
| BltBitMap-stencil | ✅ | ✅ | ✅ | ✅ |
| BltBitMap-shallow | ✅ | ✅ | ✅ | ✅ |
| ScrollRaster-directions | ✅ | ✅ | ✅ | ✅ |
| ScrollRaster-drawmodes | ✅ | ✅ | ✅ | ✅ |
| ScrollRaster-amounts | ✅ | ✅ | ✅ | ✅ |

A ❌ links to the bug it found. A `-` is untested.

Notes:
* uaegfx drops the source plane mask in `BlitPlanar2Direct`, so
`BltBitMap-planemask` fails at 24 bits and passes at 8
([amiberry#2235](https://github.com/BlitterStudio/amiberry/issues/2235)). Fixed
in WinUAE, so the column turns green once Amiberry picks that up.
* ZZ9000's `BltPattern-drawmodes` is green with
[amiberry#2237](https://github.com/BlitterStudio/amiberry/pull/2237) in the
emulated blitter -- the same zero-stride template bug as Z3660#18 below. The
shipping ZZ9000 driver needs no change of its own.


### Copperline

| scene | PAL | Z3660 |
|---|---|---|
| DrawLine-solid | ✅ | ✅ |
| DrawLine-pattern | ✅ | ✅ |
| DrawLine-jam2 | ✅ | ✅ |
| DrawLine-inversvid | ✅ | ✅ |
| DrawLine-complement | ✅ | ✅ |
| RectFill-drawmodes | ✅ | ✅ |
| RectFill-edges | ✅ | ✅ |
| RectFill-invert | ✅ | ✅ |
| ClipBlit-overlap | ✅ | ✅ |
| ClipBlit-disjoint | ✅ | ✅ |
| BltTemplate-offsets | ✅ | ✅ |
| BltTemplate-sizes | ✅ | ✅ |
| BltTemplate-drawmodes | ✅ | ✅ |
| BltTemplate-masks | ✅ | ✅ |
| BltPattern-drawmodes | ✅ | ✅ |
| BltPattern-mask | ✅ | ✅ |
| BltPattern-phase | ✅ | ✅ |
| BltBitMap-minterms | ✅ | [❌](https://github.com/BlitterStudio/zz9000-drivers/issues/57) |
| BltBitMap-offsets | ✅ | ✅ |
| BltBitMap-sizes | ✅ | ✅ |
| BltBitMap-planemask | ✅ | ✅ |
| BltBitMap-stencil | ✅ | ✅ |
| BltBitMap-shallow | ✅ | ✅ |
| ScrollRaster-directions | - | ✅ |
| ScrollRaster-drawmodes | - | ✅ |
| ScrollRaster-amounts | - | ✅ |

Notes:
* The Z3660 column is measured with two driver changes that are not merged yet,
[Z3660#18](https://github.com/shanshe/Z3660/pull/18) and
[Z3660#19](https://github.com/shanshe/Z3660/pull/19).
* Z3660#19 leaves `COMPLEMENT` lines to Picasso96, as the ZZ9000 driver already
does. `struct Line` carries no `FRST_DOT`, so an accelerated `COMPLEMENT` line
cannot tell a fresh `Draw()` from one continuing at a vertex, and inverting a
shared vertex twice restores it.
* Z3660#18 uploads the one template line a patterned blit reads. P96 sends the
`JAM2 | COMPLEMENT` tiles as a template blit whose `Template->BytesPerRow` is 0,
so sizing the upload as `BytesPerRow * h` copies nothing and leaves the board to
blit whatever the previous operation left in the template buffer. Why P96 passes
a zero stride here is unexplained.
* `BltBitMap-minterms` fails at 24 bits only, by 936 pixels. A physical ZZ9000
failed the same test by the same 936 pixels, fixed in its firmware
([zz9000-firmware#61](https://github.com/BlitterStudio/zz9000-firmware/pull/61)):
the chunky path turned `NEOR` into `EOR`, and direct color inverted the wrong
operand. Whether the Z3660 residue is the same bug, on the board or in
Copperline's emulation of it, is untested.

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

A group is one translation unit in `tests/` exporting a `P96TestGroup`; see
`tests/drawline.c`, then add it to `OBJS` and to `GROUPS` in `main.c`. A group is
named after the function it exercises and a testcase for what it does, so the
name a user types is `<group>-<test>`; `LISTTESTS` prints them all.

A testcase renders a complete scene, clearing it first, and must keep all
drawing inside the bitmap: the RastPort has no Layer, so graphics.library does
not clip it and drawing outside corrupts memory.

Scenes should be built so that a wrong driver cannot pass by accident. Drawing
solid lines in one pen, for instance, cannot detect a pixel written twice --
it takes a mode like `COMPLEMENT`, where writing twice is not the same as
writing once, and a figure whose lines actually cross.


## Coverage

What each group reaches. The P96 hook is what a card driver has to get right; a
`Default` implementation in P96's shared code stands in for any hook a driver
leaves out, so a missing hook still renders.

| group | graphics.library | P96 hook |
|---|---|---|
| DrawLine | `Move()`, `Draw()` | `DrawLine` |
| RectFill | `RectFill()` | `FillRect`, `InvertRect` |
| ClipBlit | `ClipBlit()` | `BlitRect` |
| BltTemplate | `BltTemplate()` | `BlitTemplate` |
| BltPattern | `BltPattern()` | `BlitPattern` |
| BltBitMap | `BltBitMap()`, `BltMaskBitMapRastPort()` | `BlitRectNoMaskComplete` |
| ScrollRaster | `ScrollRaster()` | `BlitRect`, `FillRect` |

Hooks with no coverage: `BlitPlanar2Chunky`, `BlitPlanar2Direct`,
`WriteYUVRect`, `ScrollPlanar`, `UpdatePlanar`. AROS calls none of them (see the
TODO in `p96gfx_rtg.h`), so scenes for these would only exercise AmigaOS.


## TODO

- `ScrollRasterBF()`: `ScrollRaster()` plus a backfill hook; extend that group.
- `EraseRect()`: its no-layer path fills through `RectFill()`; no font state to set up.
- `Text()`: renders through `BlitTemplate`; wants a write-mask sweep.
- `ClearEOL()`, `ClearScreen()`: clear to pen 0, or BPen in JAM2. A golden pins one font's metrics.
- `SetRast()`, `Flood()`, `AreaEnd()`, `DrawCircle()`, `DrawEllipse()`: uncovered.
- `WritePixelArray()` / `ReadPixelArray()` family: uncovered.
- `BltMaskBitMapRastPort()`: reached only incidentally, via `BltBitMap-stencil`.
- `BltBitMapRastPort()`: uncovered.
- A caller-level group needs a font with identical metrics under AmigaOS and AROS.

