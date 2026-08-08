// SPDX-License-Identifier: 0BSD

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "report.h"

// libnix's stdio.h declares asprintf but not vasprintf, even though
// libc.a ships it; declare it the way that header would.
extern __stdargs int vasprintf(char **__restrict strp,
                               const char *__restrict fmt, va_list ap);

// The REPORTFILE, when one is open.
static FILE *rpt_file;

// The single sink: one complete line, without the trailing newline --
// how a line ends is the sink's business.
void rpt(enum ReportKind kind, const char *line) {
    (void)kind; // stdout keeps every kind; future sinks split on it
    fputs(line, stdout);
    putchar('\n');
    if (rpt_file) {
        fputs(line, rpt_file);
        putc('\n', rpt_file);
        // Per line: a failing driver can take the machine with it, and a
        // report that stops at the fatal testcase is the useful one.
        fflush(rpt_file);
    }
}

// Duplicate every line into <path> alongside stdout, flushed per line so
// the report survives a run that takes the machine down. rpt_close() is
// safe without a matching rpt_open().
bool rpt_open(const char *path) {
    rpt_close();
    rpt_file = fopen(path, "w");
    return rpt_file != NULL;
}

void rpt_close(void) {
    if (rpt_file) {
        fclose(rpt_file);
        rpt_file = NULL;
    }
}

// The reporter itself could not allocate: emit a fixed last-resort mark
// (a GUI sink might DisplayBeep() instead).
void rpt_panic(void) {
    // Static text on purpose: this runs when malloc has already failed.
    fputs("*** OUT OF MEMORY ***\n", stdout);
}

static void vrptf(enum ReportKind kind, const char *fmt, va_list ap) {
    char *buf = NULL;

    if (vasprintf(&buf, fmt, ap) < 0) {
        rpt_panic();
        return;
    }
    rpt(kind, buf);
    free(buf);
}

// printf one whole line into rpt(); rpt_errorf() the same, tagged RPT_ERROR.
void rptf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vrptf(RPT_INFO, fmt, ap);
    va_end(ap);
}

void rpt_errorf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vrptf(RPT_ERROR, fmt, ap);
    va_end(ap);
}

// One testcase's verdict line: "PASS <time> <name>", or
// "FAIL <time> <name> <reason>" with the reason formatted printf-style.
// `micros` is how long the scene took to render.
void rpt_success(const char *name, ULONG micros) {
    char *buf = NULL;

    if (asprintf(&buf, "PASS %4lu.%03lums %s",
                 (unsigned long)(micros / 1000),
                 (unsigned long)(micros % 1000), name) < 0) {
        rpt_panic();
        return;
    }
    rpt(RPT_PASS, buf);
    free(buf);
}

void rpt_failure(const char *name, ULONG micros, const char *fmt, ...) {
    char *reason = NULL, *buf = NULL;
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vasprintf(&reason, fmt, ap);
    va_end(ap);
    if (r < 0 || asprintf(&buf, "FAIL %4lu.%03lums %-24s %s",
                          (unsigned long)(micros / 1000),
                          (unsigned long)(micros % 1000), name, reason) < 0) {
        rpt_panic();
        free(reason);
        return;
    }
    rpt(RPT_FAIL, buf);
    free(reason);
    free(buf);
}

// "skip <name>: <why>" for a testcase the run does not apply to. The time
// column is left blank so the names align with the PASS/FAIL lines.
void rpt_skip(const char *name, const char *why) {
    char *buf = NULL;

    if (asprintf(&buf, "skip %10s %s: %s", "", name, why) < 0) {
        rpt_panic();
        return;
    }
    rpt(RPT_SKIP, buf);
    free(buf);
}
