// SPDX-License-Identifier: 0BSD
//
// The screen is bigger than the scene and nothing clips a testcase's drawing --
// the RastPort has either no Layer at all or one covering the whole bitmap, and
// neither trims anything. The leftover area is walled off the way
// MungWall fences a heap allocation: painted before a testcase runs, checked
// after, and any change to it is the testcase writing where it must not.
//
// Split out of main.c, which is left with argument parsing and the run loop.

#include <proto/exec.h>
#include <graphics/rastport.h>

#include "gfx.h"
#include "report.h"
#include "rtg.h"
#include "wall.h"

// Bricks rather than one flat fill: a stray write is invisible against a wall
// painted the value it happens to write, but against alternating shades it has
// to match the brick it landed in. The shade is a function of position, so
// checking is a recomputation rather than a stored copy, and painting needs
// only RectFill -- never the primitive the testcase might be breaking.
#define WALL_CELL 32

// Odd courses start half a brick in, so the wall runs in bond like brickwork.
// Besides looking the part, it stops the vertical joints lining up, so an
// axis-aligned stray fill crosses more bricks than it would on a plain grid.
static SHORT wall_bond(SHORT y) {
    return (SHORT)(((y / WALL_CELL) & 1) * (WALL_CELL / 2));
}

// Four dark shades of brick, taken from the 3-3-2 cube in palette.c so a
// palette run and a truecolor run look the same. They share a green gun and no
// blue, varying only in red, which keeps them a single dark brown-to-brick-red
// family: the wall frames a scene and should not compete with it for attention.
//
// Detection needs colors a testcase would not write, not many colors -- a stray
// fill in one shade still shows up against the other three. None is pen 0, so
// cleared memory does not read as an intact wall.
static const struct {
    UBYTE pen;
    ULONG rgb;
} WALL_SHADE[4] = {
    {9, 0x00242400UL},  // darkest
    {10, 0x00482400UL}, // dark brown
    {11, 0x006C2400UL}, // brown
    {12, 0x00902400UL}, // brick red
};

// Stepping the course by two keeps every brick a different shade from the one
// beside it and from either brick it overlaps in the course below, so no two
// touching bricks ever merge: the bond shifts the brick index by 0 or 1 between
// courses, and neither leaves the shade unchanged.
static int wall_shade(SHORT x, SHORT y) {
    const SHORT course = (SHORT)(y / WALL_CELL);
    const SHORT brick = (SHORT)((x + wall_bond(y)) / WALL_CELL);

    return (brick + 2 * course) & 3;
}

static void build_wall(struct RastPort *rp, SHORT bx, SHORT by, SHORT bw,
                       SHORT bh) {
    SHORT y = by;

    // Bricks sit on an absolute grid, so the first course or column of a wall
    // that does not start on a boundary is partial.
    while (y < by + bh) {
        SHORT y2 = (SHORT)((y / WALL_CELL + 1) * WALL_CELL - 1);
        const SHORT bond = wall_bond(y);
        SHORT x = bx;

        if (y2 > by + bh - 1)
            y2 = (SHORT)(by + bh - 1);
        while (x < bx + bw) {
            SHORT x2 =
                (SHORT)(((x + bond) / WALL_CELL + 1) * WALL_CELL - bond - 1);
            const int s = wall_shade(x, y);

            if (x2 > bx + bw - 1)
                x2 = (SHORT)(bx + bw - 1);
            gfx_fill(rp, x, y, x2, y2,
                     gfx_color(WALL_SHADE[s].pen, WALL_SHADE[s].rgb));
            x = (SHORT)(x2 + 1);
        }
        y = (SHORT)(y2 + 1);
    }
}

void build_walls(struct RastPort *rp, const struct Wall *wall) {
    rp->Mask = 0xFF;
    if (wall->screen_w > wall->scene_w)
        build_wall(rp, wall->scene_w, 0,
                   (SHORT)(wall->screen_w - wall->scene_w), wall->screen_h);
    if (wall->screen_h > wall->scene_h)
        build_wall(rp, 0, wall->scene_h, wall->scene_w,
                   (SHORT)(wall->screen_h - wall->scene_h));
}

// True if a wall was written through, naming the first pixel found.
bool wall_broken(struct RastPort *rp, const struct Wall *wall,
                 const char *name) {
    const SHORT rect[2][4] = {
        {wall->scene_w, 0, (SHORT)(wall->screen_w - wall->scene_w),
         wall->screen_h},
        {0, wall->scene_h, wall->scene_w,
         (SHORT)(wall->screen_h - wall->scene_h)},
    };

    for (int i = 0; i < 2; i++) {
        const SHORT bx = rect[i][0], by = rect[i][1];
        const SHORT bw = rect[i][2], bh = rect[i][3];
        UBYTE *px;

        if (bw <= 0 || bh <= 0)
            continue;
        px = wall->bpp == 3 ? rtg_read_rgb(rp, bx, by, bw, bh)
                            : gfx_read_pens(rp, bx, by, bw, bh, wall->depth);
        if (!px)
            continue; // no memory to check with is not evidence of overdraw

        for (SHORT y = 0; y < bh; y++)
            for (SHORT x = 0; x < bw; x++) {
                const UBYTE *q = px + ((ULONG)y * bw + x) * wall->bpp;
                const int s = wall_shade((SHORT)(bx + x), (SHORT)(by + y));
                const ULONG rgb = WALL_SHADE[s].rgb;

                if (wall->bpp == 3 ? q[0] == (UBYTE)(rgb >> 16) &&
                                         q[1] == (UBYTE)(rgb >> 8) &&
                                         q[2] == (UBYTE)rgb
                                   : q[0] == WALL_SHADE[s].pen)
                    continue;
                rpt_failure(name, "drew outside the scene at %d,%d",
                               bx + x, by + y);
                FreeVec(px);
                return true;
            }
        FreeVec(px);
    }
    return false;
}
