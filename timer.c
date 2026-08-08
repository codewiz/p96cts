// SPDX-License-Identifier: 0BSD
//
// Scene timing. ReadEClock() gives a monotonic 64-bit tick count at the E
// clock rate (~709 kHz PAL) in one cheap call, immune to the system clock
// being set mid-run -- which is what rules out GetSysTime(), and DateStamp()
// only ticks 50 times a second. timer.device is opened only to reach the
// library call; no request is ever sent.

#include <proto/exec.h>
#include <proto/timer.h>
#include <devices/timer.h>

#include "timer.h"

// Not static: the proto header declares it extern.
struct Device *TimerBase;
static struct timerequest timer_req;
static ULONG eclock_freq;

bool timer_open(void) {
    struct EClockVal now;

    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ,
                   (struct IORequest *)&timer_req, 0))
        return false;
    TimerBase = timer_req.tr_node.io_Device;
    eclock_freq = ReadEClock(&now);
    return true;
}

void timer_close(void) {
    if (TimerBase) {
        CloseDevice((struct IORequest *)&timer_req);
        TimerBase = NULL;
    }
}

// The current E clock, for a later eclock_micros().
void timer_now(struct EClockVal *now) {
    ReadEClock(now);
}

// Microseconds elapsed from t0 to t1.
ULONG eclock_micros(const struct EClockVal *t0, const struct EClockVal *t1) {
    const unsigned long long a =
        ((unsigned long long)t0->ev_hi << 32) | t0->ev_lo;
    const unsigned long long b =
        ((unsigned long long)t1->ev_hi << 32) | t1->ev_lo;

    return (ULONG)((b - a) * 1000000ULL / eclock_freq);
}
