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

void rpt_success(const char *name, ULONG micros) {
    char *buf = NULL;

    if (asprintf(&buf, "PASS %-24s %4lu.%03lums", name,
                 (unsigned long)(micros / 1000),
                 (unsigned long)(micros % 1000)) < 0) {
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
    if (r < 0 || asprintf(&buf, "FAIL %-24s %4lu.%03lums %s", name,
                          (unsigned long)(micros / 1000),
                          (unsigned long)(micros % 1000), reason) < 0) {
        rpt_panic();
        free(reason);
        return;
    }
    rpt(RPT_FAIL, buf);
    free(reason);
    free(buf);
}

void rpt_skip(const char *name, const char *why) {
    char *buf = NULL;

    if (asprintf(&buf, "skip %s: %s", name, why) < 0) {
        rpt_panic();
        return;
    }
    rpt(RPT_SKIP, buf);
    free(buf);
}
