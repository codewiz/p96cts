/* SPDX-License-Identifier: 0BSD */

/* DrawLine() testcases.
 *
 * P96 specifies DrawLine as Bresenham stepping over a 16-bit rotating
 * LinePtrn, so the scenes sweep every octant and both major axes and vary the
 * draw mode, which is where a driver's line code diverges from the reference.
 */

#include <proto/graphics.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>

#include "p96cts.h"

/* Unit vectors * 1024 at 15-degree steps: every octant and both major axes,
 * which is what Bresenham stepping is defined over. */
static const short DIRS[24][2] = {
    {1024, 0},    {989, 265},   {887, 512},   {724, 724},   {512, 887},
    {265, 989},   {0, 1024},    {-265, 989},  {-512, 887},  {-724, 724},
    {-887, 512},  {-989, 265},  {-1024, 0},   {-989, -265}, {-887, -512},
    {-724, -724}, {-512, -887}, {-265, -989}, {0, -1024},   {265, -989},
    {512, -887},  {724, -724},  {887, -512},  {989, -265},
};

/* A star of 24 rays centred in the scene. The radius keeps every ray inside
 * the bitmap: a RastPort without a Layer is not clipped by graphics.library,
 * so a ray leaving the bitmap would corrupt memory rather than be clipped. */
static void star(struct RastPort *rp, SHORT w, SHORT h, UWORD pattern) {
    int cx = w / 2, cy = h / 2, k;
    int r = ((w < h ? w : h) / 2) - 8;

    SetDrPt(rp, pattern);
    for (k = 0; k < 24; k++) {
        Move(rp, cx, cy);
        Draw(rp, cx + DIRS[k][0] * r / 1024, cy + DIRS[k][1] * r / 1024);
    }
    SetDrPt(rp, 0xFFFF);
}

static void t_solid(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    SetAPen(rp, 1);
    star(rp, w, h, 0xFFFF);
}

static void t_pattern(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1);
    SetAPen(rp, 1);
    star(rp, w, h, 0xF0F0);
}

static void t_jam2(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM2);
    SetAPen(rp, 1);
    SetBPen(rp, 2); /* pattern gaps get the background pen */
    star(rp, w, h, 0xF0F0);
    SetBPen(rp, 0);
}

static void t_inversvid(struct RastPort *rp, SHORT w, SHORT h) {
    p96cts_clear(rp, w, h, 0);
    SetDrMd(rp, JAM1 | INVERSVID); /* the pattern is inverted */
    SetAPen(rp, 1);
    star(rp, w, h, 0xF0F0);
}

/* Pentagon vertices * 1024, every 72 degrees starting at the top. */
static const short PENTA[5][2] = {
    {0, -1024}, {974, -316}, {602, 828}, {-602, 828}, {-974, 316},
};

/* A closed pentagram: visiting the pentagon's vertices two apart draws five
 * lines that cross at five interior points. Those crossings are drawn twice,
 * so under COMPLEMENT they inverse twice and must come back to exactly the
 * background value -- which is what makes this scene test XOR semantics
 * rather than just "some pixels changed". */
static void pentagram(struct RastPort *rp, SHORT w, SHORT h) {
    static const int ORDER[6] = {0, 2, 4, 1, 3, 0};
    int cx = w / 2, cy = h / 2;
    int r = ((w < h ? w : h) / 2) - 8;
    int k;

    Move(rp, cx + PENTA[ORDER[0]][0] * r / 1024,
         cy + PENTA[ORDER[0]][1] * r / 1024);
    for (k = 1; k < 6; k++)
        Draw(rp, cx + PENTA[ORDER[k]][0] * r / 1024,
             cy + PENTA[ORDER[k]][1] * r / 1024);
}

/* Backgrounds for the COMPLEMENT scene. Deliberately not 0 and 1: inverting
 * pen 0 gives 255, which a driver that just writes all-ones would get right by
 * accident, and neither pen exercises more than one bit. These two spread
 * their bits across the byte and invert to 0xCA and 0x59, so all eight bit
 * positions are checked in both directions. */
#define COMPLEMENT_BG_LEFT 0x35
#define COMPLEMENT_BG_RIGHT 0xA6

static void t_complement(struct RastPort *rp, SHORT w, SHORT h) {
    /* Two background tones, so the same COMPLEMENT produces two different
     * results in one scene and a driver that ignores the destination cannot
     * pass by writing a constant. */
    p96cts_clear(rp, w, h, COMPLEMENT_BG_LEFT);
    SetDrMd(rp, JAM1);
    SetAPen(rp, COMPLEMENT_BG_RIGHT);
    RectFill(rp, w / 2, 0, w - 1, h - 1);

    SetDrMd(rp, COMPLEMENT);
    SetAPen(rp, 1);
    pentagram(rp, w, h);
}

static const struct P96Test TESTS[] = {
    {"solid", t_solid},
    {"pattern", t_pattern},
    {"jam2", t_jam2},
    {"inversvid", t_inversvid},
    {"complement", t_complement},
};

const struct P96TestGroup DrawLineGroup = {
    "drawline", TESTS, (int)(sizeof TESTS / sizeof TESTS[0]), 1 /* clut_only */
};
