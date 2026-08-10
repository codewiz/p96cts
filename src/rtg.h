// SPDX-License-Identifier: 0BSD
//
// Everything rtg.c offers: the RTG library calls the harness and the scenes
// make, with the choice of RTG library hidden behind them.

#ifndef P96CTS_RTG_H
#define P96CTS_RTG_H

#include <exec/types.h>
#include <stdbool.h>

struct BitMap;
struct RastPort;
struct Screen;

bool rtg_open(void);
void rtg_versions(void);
void rtg_close(void);

struct BitMap *rtg_alloc_reference(struct Screen *scr, int height, int depth);
void rtg_free_bitmap(struct BitMap *bm);

struct BitMap *rtg_alloc_friend(struct BitMap *like, SHORT w, SHORT h);
void rtg_free_friend(struct BitMap *bm);

ULONG rtg_rgbformat(struct BitMap *bm);
const char *rtg_format_name(ULONG fmt);
bool rtg_format_by_pen(ULONG fmt);

void rtg_fill_rgb(struct RastPort *rp, SHORT x1, SHORT y1, SHORT x2, SHORT y2,
                  ULONG rgb);
UBYTE *rtg_read_rgb(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h);

void rtg_write_rgb(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                   UBYTE *px, SHORT sx, SHORT sy, SHORT stride);
void rtg_write_pens(struct RastPort *rp, SHORT x0, SHORT y0, SHORT w, SHORT h,
                    UBYTE *px, SHORT sx, SHORT sy, SHORT stride);

#endif
