/* testharn.c -- implementation of the unit-test harness.
 *
 * Three things leave the guest: TEST.OUT (the transcript the host parses),
 * HB.TXT (the heartbeat the host polls to tell a slow run from a hung one) and
 * RUN.DON (the completion marker).  All three are written with
 * fopen/fprintf/fclose rather than a held handle: DOSBox-X commits a redirected
 * write to the host file only on close, so a run that wedges half way still
 * leaves the host everything it had produced up to that point, with the
 * heartbeat frozen on the name of the test that hung.
 */
#include <stdio.h>
#include "testharn.h"

int test_total = 0;
int test_failed = 0;

/* Every string that reaches the transcript is copied through a bounded buffer
   first. The expression text comes from stringifying whatever was written at
   the CHECK_EQ site, so its length is not under this file's control, and a
   DOS/4G image has no guard page: an overrun would quietly rewrite the counters
   sitting next to the buffer and could turn a failing run into a passing one.
   NAME_MAX + EXPR_MAX plus the fixed text and the three numbers stays well
   inside LINE_MAX. */
#define LINE_MAX 512
#define NAME_MAX 96
#define EXPR_MAX 160

static char *current = "(none)";
static int current_check = 0;
static char line_buf[LINE_MAX];
static char name_buf[NAME_MAX];
static char expr_buf[EXPR_MAX];

static void copy_bounded(char *dst, char *src, int limit)
{
    int i;

    for (i = 0; i < limit - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void emit(char *line)
{
    FILE *f;

    f = fopen("TEST.OUT", "a");
    if (f == NULL) {
        return;
    }
    fprintf(f, "%s\n", line);
    fclose(f);
}

void test_start(void)
{
    FILE *f;

    f = fopen("TEST.OUT", "w");
    if (f != NULL) {
        fclose(f);
    }
}

void test_begin(char *name)
{
    FILE *f;

    current = name;
    current_check = 0;
    f = fopen("HB.TXT", "w");
    if (f != NULL) {
        fprintf(f, "%s\n", name);
        fclose(f);
    }
    sprintf(line_buf, "=== %s", name);
    emit(line_buf);
}

void test_check(char *expr, long got, long want)
{
    test_total++;
    current_check++;
    if (got == want) {
        return;
    }
    test_failed++;
    copy_bounded(name_buf, current, NAME_MAX);
    copy_bounded(expr_buf, expr, EXPR_MAX);
    sprintf(line_buf, "FAIL %s check %d: %s -> %ld, expected %ld",
            name_buf, current_check, expr_buf, got, want);
    emit(line_buf);
}

void test_report(void)
{
    FILE *f;

    sprintf(line_buf, "total=%d failed=%d", test_total, test_failed);
    emit(line_buf);
    emit(test_failed == 0 ? "verdict=ok" : "verdict=fail");

    /* Last, and only here: the host reads the marker as "the run finished on
       its own", so it must not appear before the transcript is complete. */
    f = fopen("RUN.DON", "w");
    if (f != NULL) {
        fprintf(f, "done\n");
        fclose(f);
    }
}
