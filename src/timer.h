// SPDX-License-Identifier: 0BSD
//
// Scene timing over timer.device's ReadEClock(): see timer.c.

#ifndef P96CTS_TIMER_H
#define P96CTS_TIMER_H

#include <devices/timer.h>
#include <stdbool.h>

bool timer_open(void);
void timer_close(void);

void timer_now(struct EClockVal *now);
ULONG eclock_micros(const struct EClockVal *t0, const struct EClockVal *t1);

#endif
