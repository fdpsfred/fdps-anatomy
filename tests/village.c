/* tests/village.c -- cover for src/village.c.
 *
 * Expected values come from the assembly at 000357a0 and from the 192 table
 * bytes the image holds at 000310c0, cross-checked against the strategy
 * guide's own dump of the same table (docs/guide, fdps/modify2: "each chapter
 * 8 bytes, from chapter 2 through chapter 25, beginning 4D 50 4D 1E").  None
 * of them is read off the emitted C.
 *
 * Both globals the function touches are set by every test before it calls, so
 * nothing here depends on what the not-yet-emitted data definitions hold.
 */
#include "testharn.h"
#include "gamedata.h"
#include "village.h"

/* Chapter id 5 is the player's chapter 6, whose row is 1f 12 2e 13 12 14 --
   S E C R E T.  Every keystroke but the last leaves a non-zero byte ahead of
   the position, so CMP byte ptr [...],0 fails and the function reports 0; the
   sixth advances the position to 6, where the row's terminator sits, and the
   JZ takes the return-1 path. */
static void village_secret_code_completes(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 2);
    CHECK_EQ(fdps_check_secret_code_key(0x2e), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 3);
    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 5);
    CHECK_EQ(fdps_check_secret_code_key(0x14), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 6);
}

/* The row selector is chapter id minus one: the effective base of the indexing
   is the stack buffer minus one row, [EAX*8 + EBP - 0xcc] against a buffer at
   EBP - 0xc4.  Chapter id 1 therefore uses row 0, 4d 50 4d 1e -- right, down,
   right, A -- which is the first code the guide lists, for the player's
   chapter 2. */
static void village_row_is_chapter_id_minus_one(void)
{
    data_fdps_chapter_current_chapter_id = 1;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* The same keystrokes under chapter id 2, whose row is the next one down --
   4d 4b 50 4d 1e, right left down right A.  The second key already breaks the
   match, which is what an emit that indexed the table with the chapter id
   itself would get wrong: it would answer 1 on the fourth key here. */
static void village_next_chapter_has_the_next_row(void)
{
    data_fdps_chapter_current_chapter_id = 2;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4b), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 1);
}

/* Nothing resets the position on entry: the first compare reads the row at
   whatever the global already holds.  Position 3 of the SECRET row is 0x13, R,
   and matching it moves the position on to 4 without completing anything. */
static void village_resumes_at_stored_position(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* A key that is neither the expected byte nor the row's first byte throws the
   attempt away: MOV [0x601c0],0, then the second compare fails too and the
   position is left at 0.  0x30 is B, which the SECRET row does not contain. */
static void village_mismatch_resets_position(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x30), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
}

/* A key that breaks the run but happens to be the code's own first byte starts
   the next attempt on that same keystroke: the zero store is followed by
   MOV [0x601c0],1.  S at position 3 -- where R was expected -- is the case. */
static void village_mismatch_restarts_on_first_byte(void)
{
    data_fdps_chapter_current_chapter_id = 5;
    data_fdps_secret_code_match_pos = 3;

    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 1);
}

/* Row 15 -- chapter id 16, the player's chapter 17 -- is eight zero bytes, and
   so are rows 16, 20 and 21.  The caller never passes scancode 0, so the first
   compare can never hold, the restart compare against the row's zero first
   byte cannot hold either, and the position is pinned at 0 for good. */
static void village_chapter_without_a_code(void)
{
    data_fdps_chapter_current_chapter_id = 16;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x4d), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
    CHECK_EQ(fdps_check_secret_code_key(0x01), 0);
    CHECK_EQ(data_fdps_secret_code_match_pos, 0);
}

/* The longest code in the table is row 7, chapter id 8: 30 1e 13 31 1e 20 18,
   B A R N A D O, seven keys with the terminator in the row's last byte.  It is
   the terminator that ends the code, not the position reaching 8 -- that half
   of the || is unreachable with this table -- and the position is left at 7. */
static void village_longest_code_is_seven_keys(void)
{
    data_fdps_chapter_current_chapter_id = 8;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x30), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x13), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x31), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1e), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x20), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x18), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 7);
}

/* The table's last row is 23, chapter id 24, the player's chapter 25 -- the
   last chapter the guide lists a code for.  11 12 1f 14 is W E S T. */
static void village_last_chapter_code(void)
{
    data_fdps_chapter_current_chapter_id = 24;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x11), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x12), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x1f), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x14), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

/* A two-key code, row 13 for chapter id 14: 02 06, the digits 1 and 5.  The
   completion is reported on the second key even though six bytes of the row
   are still ahead of the position. */
static void village_two_key_code(void)
{
    data_fdps_chapter_current_chapter_id = 14;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x02), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x06), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 2);
}

/* A code whose bytes repeat: row 22, chapter id 23, is 50 50 50 50 -- the down
   arrow four times.  Each key both extends the run and equals the row's first
   byte, so the restart branch is never reached and the count is the run
   length. */
static void village_repeated_key_code(void)
{
    data_fdps_chapter_current_chapter_id = 23;
    data_fdps_secret_code_match_pos = 0;

    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 0);
    CHECK_EQ(fdps_check_secret_code_key(0x50), 1);
    CHECK_EQ(data_fdps_secret_code_match_pos, 4);
}

void run_village_tests(void)
{
    RUN_TEST(village_secret_code_completes);
    RUN_TEST(village_row_is_chapter_id_minus_one);
    RUN_TEST(village_next_chapter_has_the_next_row);
    RUN_TEST(village_resumes_at_stored_position);
    RUN_TEST(village_mismatch_resets_position);
    RUN_TEST(village_mismatch_restarts_on_first_byte);
    RUN_TEST(village_chapter_without_a_code);
    RUN_TEST(village_longest_code_is_seven_keys);
    RUN_TEST(village_last_chapter_code);
    RUN_TEST(village_two_key_code);
    RUN_TEST(village_repeated_key_code);
}
