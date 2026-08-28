/* testharn.h -- the unit-test harness the emitted code is exercised through.
 *
 * Tests live in their own translation units and link against the production
 * sources without modifying them (ADR-0003): there is no conditional
 * compilation, no hook and no test-only branch anywhere under src/.
 *
 * One test file mirrors one source file: tests/<stem>.c covers src/<stem>.c and
 * defines `void run_<stem>_tests(void)`.  tools/code_emit/build_emit.py finds
 * that function by name and generates the runner list, so a new test file needs
 * no wiring anywhere.  A tests/ file that defines no such function is compiled
 * as a support unit and never called directly.
 *
 * Everything is C89 and every name fits 8.3, because this is compiled by the
 * DOS-hosted Watcom 10.0a inside DOSBox-X like the rest of the rebuild.
 */
#ifndef TESTHARN_H
#define TESTHARN_H

extern int test_total;  /* checks run */
extern int test_failed; /* checks that did not hold */

extern void test_start(void);
extern void test_begin(char *name);
extern void test_check(char *expr, long got, long want);
extern void test_report(void);

/* The expression text is captured so a failure line says what was evaluated,
   not just which numbers disagreed.  Everything widens to long: the emitted
   code returns int, char and unsigned of several widths, and one comparison
   type keeps the harness from needing a macro per width.

   A failing check is located by its test name and its ordinal within that
   test, not by __LINE__: wcc386 10.0a gets __LINE__ right only when the macro
   invocation begins a line, and reports a line number from the preprocessed
   stream -- past the end of the file -- when it follows other tokens on the
   same line.  A confidently wrong location is worse than none. */
#define CHECK_EQ(expr, want) \
    test_check(#expr, (long)(expr), (long)(want))

#define RUN_TEST(fn) \
    do { test_begin(#fn); fn(); } while (0)

#endif
