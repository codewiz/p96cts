// SPDX-License-Identifier: 0BSD
//
// Every line a run prints goes through rpt(): the formatting helpers
// build one whole line per call and tag it with what it is, so a future
// sink -- a REPORTFILE next to stdout, ANSI color on one but not the
// other, a GUI window -- only ever touches report.c and can color or
// route lines by kind without parsing them.

#ifndef P96CTS_REPORT_H
#define P96CTS_REPORT_H

#include <stdbool.h>

enum ReportKind {
    RPT_INFO,  // banners, progress, diff detail
    RPT_ERROR, // the run cannot do what was asked
    RPT_PASS,
    RPT_FAIL,
    RPT_SKIP,
};

// The single sink: one complete line, without the trailing newline --
// how a line ends is the sink's business.
void rpt(enum ReportKind kind, const char *line);

// Duplicate every line into <path> alongside stdout, flushed per line so
// the report survives a run that takes the machine down. rpt_close() is
// safe without a matching rpt_open().
bool rpt_open(const char *path);
void rpt_close(void);

// printf one whole line into rpt().
void rptf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rpt_errorf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// One testcase's verdict line: "PASS <name>", or "FAIL <name> <reason>"
// with the reason formatted printf-style.
void rpt_success(const char *name);
void rpt_failure(const char *name, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

// "skip <name>: <why>" for a testcase the run does not apply to.
void rpt_skip(const char *name, const char *why);

// The reporter itself could not allocate: emit a fixed last-resort mark
// (a GUI sink might DisplayBeep() instead).
void rpt_panic(void);

#endif
