// SPDX-License-Identifier: 0BSD
//
// Guard walls around a scene: see wall.c.

#ifndef P96CTS_WALL_H
#define P96CTS_WALL_H

#include <exec/types.h>
#include <stdbool.h>

struct RastPort;

// A scene rendered at the origin of a larger screen. The walls are what is
// left of the screen: to the right of the scene, and below it.
struct Wall {
    SHORT scene_w, scene_h;
    SHORT screen_w, screen_h;
    int bpp;    // bytes per pixel the run compares in, 1 or 3
    int depth;
};

void build_walls(struct RastPort *rp, const struct Wall *wall);
bool wall_broken(struct RastPort *rp, const struct Wall *wall,
                 const char *name);

#endif
