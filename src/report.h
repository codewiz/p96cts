// SPDX-License-Identifier: 0BSD
//
// Every line a run prints goes through rpt(): the formatting helpers
// build one whole line per call and tag it with what it is, so a future
// sink -- a REPORTFILE next to stdout, ANSI color on one but not the
// other, a GUI window -- only ever touches report.c and can color or
// route lines by kind without parsing them.

#ifndef P96CTS_REPORT_H
#define P96CTS_REPORT_H

#include <exec/types.h>
#include <stdbool.h>

enum ReportKind {
    RPT_INFO,  // banners, progress, diff detail
    RPT_ERROR, // the run cannot do what was asked
    RPT_PASS,
    RPT_FAIL,
    RPT_SKIP,
};

void rpt(enum ReportKind kind, const char *line);

bool rpt_open(const char *path);
void rpt_close(void);

void rptf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rpt_errorf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void rpt_success(const char *name, ULONG micros);
void rpt_failure(const char *name, ULONG micros, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

void rpt_skip(const char *name, const char *why);

void rpt_panic(void);

#endif
