// SPDX-License-Identifier: 0BSD
//
// The display database: turning a WxHxD request into a display id, and dumping
// what the database holds so a usable mode can be picked.

#ifndef P96CTS_MODES_H
#define P96CTS_MODES_H

#include <exec/types.h>

// No display id matched. graphics/modeid.h defines INVALID_ID as ~0, an int,
// so comparing it against a ULONG display id is a signedness mismatch.
#define INVALID_MODE ((ULONG)~0UL)

ULONG find_mode(int w, int h, int depth, const char *monitor, char *name_out,
                int name_len);
void list_modes(void);

#endif
