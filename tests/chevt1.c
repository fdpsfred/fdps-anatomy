/* tests/chevt1.c -- cover for src/chevt1.c.
 *
 * Expected values come from the assembly of fdps_chapter_event_set_game_over
 * at 00036cd0, which is thirty-four bytes of straight line with no compare and
 * no branch anywhere in it: PUSH EBX/ESI/EDI/EBP and MOV EBP,ESP, then MOV
 * dword ptr [EBP + 0x14],0x0 at 00036cdc over the argument slot, then MOV
 * dword ptr [0x00069da0],0x1 at 00036ce3, then the four POPs and RET.  1 is a
 * literal in the instruction, not a value read from anywhere, and there is no
 * instruction in front of the store that could skip it.
 *
 * That the code means defeat comes from the dispatch at 0002936b, which loads
 * the same global and separates 1 from 2 with unsigned compares at 00029377
 * and 0002937d; the chapter-cleared code 2 is what the chapter 15, 22 and 23
 * boss-defeat handlers store, and 0 is what the chapter state reset installs.
 *
 * Nothing below asserts what the global holds before a call: it is a
 * ticket 23 symbol and the build links it zero-filled for now, so every case
 * writes the state it wants to see changed.
 *
 * The chapter 2 handler at 00036d00 is covered from the same place: its seven
 * unit indices, the inclusive compares that produce them, and the 0xf0 mask
 * are all literals in its instruction stream.  Its unit array is staged here
 * rather than read from a game file, because the handler takes its whole
 * effect through data_fdps_map_unit_array_ptr -- pointing that global at a
 * local block is the only way to see the stores.  What the global itself holds
 * is ticket 23's and is not asserted.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt1.h"

/* The three codes the battle-end global carries, from the stores across the
   image: 0 from the chapter state reset at 00022766, 1 from this handler at
   00036ce3, 2 from the boss-defeat handlers at 00037caf and 00038936. */
#define END_CODE_RUNNING 0
#define END_CODE_DEFEAT  1
#define END_CODE_CLEARED 2

/* The store is reached from a running battle, which is the state the loops
   keep the global in, and the defeat code lands in it. */
static void set_game_over_from_running(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* No test guards the store, so a chapter already marked cleared is overwritten
   into a defeat rather than left alone.  This is the case that would come out
   differently if the emitted C had grown a compare the assembly does not
   have. */
static void set_game_over_overwrites_cleared(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_CLEARED;
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* Storing a literal over the same address twice leaves the same value, so the
   handler firing again on a later event does not accumulate or toggle. */
static void set_game_over_is_idempotent(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(0);
    fdps_chapter_event_set_game_over(0);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* The argument slot is written and never read, and no unit record is resolved
   anywhere in the body, so the index the dispatcher passes cannot reach the
   result.  The turn-event dispatcher pushes a literal 0 at 0002e13e; the cell
   search and the death-script runner push a real unit index, and a scripted
   index out of the map file is not range checked on the way in.  All three
   have to leave the global holding the defeat code. */
static void set_game_over_ignores_unit_index(void)
{
    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(7);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);

    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(-1);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);

    data_fdps_chapter_event_or_battle_end_code = END_CODE_RUNNING;
    fdps_chapter_event_set_game_over(30000);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* ---- fdps_chapter_02_event_enemies_advance, 00036d00 -------------------- */

/* The seven indices the four inline loops reach, read off the constants at
   00036d13/00036d1a (0x13..0x15), 00036d73/00036d7a (0x11..0x11),
   00036dd3/00036dda (0x0d..0x0d) and 00036e33/00036e3a (8..9), with the
   compare at 00036d43 and its three copies being the signed inclusive JLE. */
#define ADVANCE_INDEX_COUNT 7
static int advance_indices[ADVANCE_INDEX_COUNT] = {8, 9, 0x0d, 0x11,
                                                   0x13, 0x14, 0x15};

/* Two records past the highest index the handler writes, so an off-by-one at
   the top end of the 0x13..0x15 range has somewhere visible to land. */
#define ADVANCE_STAGE_UNITS 24

static struct fdps_unit_record advance_units[ADVANCE_STAGE_UNITS];

static int index_is_advanced(int unit_index)
{
    int i;

    for (i = 0; i < ADVANCE_INDEX_COUNT; i++) {
        if (advance_indices[i] == unit_index) {
            return 1;
        }
    }
    return 0;
}

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_advance_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) advance_units;
    for (i = 0; i < (int) sizeof(advance_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < ADVANCE_STAGE_UNITS; i++) {
        advance_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) advance_units;
}

/* The record has to be 0x50 bytes with its AI byte at +0x34 for the emitted C
   to address the byte the four MOV byte ptr [EAX+0x34] stores address; the
   stride is the IMUL 0x50 inside fdps_get_unit_record. */
static void advance_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Exactly the seven indices the four ranges cover are rewritten and every
   other record in the block is left as it was.  The staged 0x52 is behaviour
   code 2 -- hold position, which is what chapter 2's map deploys these units
   in -- under a high nibble of 0x50; the seven come out 0x50 because the mode
   ORed in is 0, and the rest keep 0x52.  This is the case that pins the range
   boundaries: 0x12 and 0x16 either side of the three-unit range, 0x10 and 0x12
   either side of the 0x11 range, 0x0c and 0x0e either side of the 0x0d range,
   and 7 and 0x0a either side of the 8..9 range are all in the block and all
   have to be untouched. */
static void advance_clears_exactly_the_seven_indices(void)
{
    int i;

    stage_advance_units(0x52);
    fdps_chapter_02_event_enemies_advance(0);

    for (i = 0; i < ADVANCE_STAGE_UNITS; i++) {
        if (index_is_advanced(i)) {
            CHECK_EQ(advance_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(advance_units[i].ai_behavior, 0x52);
        }
    }
}

/* Both single-index ranges are inclusive, so each runs once rather than not at
   all: the JLE at 00036da3 and 00036e03 compares a counter seeded with the
   first index against a bound holding the same index.  A range written with <
   instead of <= would leave 0x11 and 0x0d in mode 2. */
static void advance_single_index_ranges_run_once(void)
{
    stage_advance_units(0x52);
    fdps_chapter_02_event_enemies_advance(0);

    CHECK_EQ(advance_units[0x11].ai_behavior, 0x50);
    CHECK_EQ(advance_units[0x0d].ai_behavior, 0x50);
}

/* The high nibble is carried across untouched, one AND 0xf0 per loop, and the
   low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI flags
   read elsewhere, so a merge that assigned the mode whole -- or that masked
   with anything wider -- would drop them.  Expected values are the staged byte
   ANDed with 0xf0. */
static void advance_keeps_the_high_nibble(void)
{
    stage_advance_units(0);
    advance_units[8].ai_behavior = 0xc2;
    advance_units[9].ai_behavior = 0x02;
    advance_units[0x0d].ai_behavior = 0xff;
    advance_units[0x11].ai_behavior = 0x40;
    advance_units[0x13].ai_behavior = 0x8b;
    advance_units[0x14].ai_behavior = 0x00;
    advance_units[0x15].ai_behavior = 0x0f;

    fdps_chapter_02_event_enemies_advance(0);

    CHECK_EQ(advance_units[8].ai_behavior, 0xc0);
    CHECK_EQ(advance_units[9].ai_behavior, 0x00);
    CHECK_EQ(advance_units[0x0d].ai_behavior, 0xf0);
    CHECK_EQ(advance_units[0x11].ai_behavior, 0x40);
    CHECK_EQ(advance_units[0x13].ai_behavior, 0x80);
    CHECK_EQ(advance_units[0x14].ai_behavior, 0x00);
    CHECK_EQ(advance_units[0x15].ai_behavior, 0x00);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too. */
static void advance_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) advance_units;
    for (i = 0; i < (int) sizeof(advance_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_02_event_enemies_advance(0);

    CHECK_EQ(bytes[0x13 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x13 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x13 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[8 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x35], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 00036d0c and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The turn-event dispatcher pushes a literal 0 at 0002e13e,
   which is the path this handler is actually reached by; the cell search and
   the death-script runner push a real, unchecked unit index.  Index 3 is
   asserted unchanged because it is one an argument-driven handler would have
   written. */
static void advance_ignores_the_unit_index_argument(void)
{
    stage_advance_units(0x52);
    fdps_chapter_02_event_enemies_advance(3);
    CHECK_EQ(advance_units[3].ai_behavior, 0x52);
    CHECK_EQ(advance_units[8].ai_behavior, 0x50);

    stage_advance_units(0x52);
    fdps_chapter_02_event_enemies_advance(-1);
    CHECK_EQ(advance_units[0x15].ai_behavior, 0x50);
    CHECK_EQ(advance_units[0x16].ai_behavior, 0x52);

    stage_advance_units(0x52);
    fdps_chapter_02_event_enemies_advance(30000);
    CHECK_EQ(advance_units[0x11].ai_behavior, 0x50);
    CHECK_EQ(advance_units[0x10].ai_behavior, 0x52);
}

/* The merge is idempotent: a record already in mode 0 keeps its high nibble
   and stays in mode 0, so a second firing of the event -- or the handler being
   reached through the cell search after the turn event already ran -- adds
   nothing and takes nothing away. */
static void advance_run_twice_changes_nothing_more(void)
{
    stage_advance_units(0xc2);
    fdps_chapter_02_event_enemies_advance(0);
    CHECK_EQ(advance_units[9].ai_behavior, 0xc0);

    fdps_chapter_02_event_enemies_advance(0);
    CHECK_EQ(advance_units[9].ai_behavior, 0xc0);
    CHECK_EQ(advance_units[0x0a].ai_behavior, 0xc2);
}

void run_chevt1_tests(void)
{
    RUN_TEST(set_game_over_from_running);
    RUN_TEST(set_game_over_overwrites_cleared);
    RUN_TEST(set_game_over_is_idempotent);
    RUN_TEST(set_game_over_ignores_unit_index);
    RUN_TEST(advance_record_shape_matches_the_offsets);
    RUN_TEST(advance_clears_exactly_the_seven_indices);
    RUN_TEST(advance_single_index_ranges_run_once);
    RUN_TEST(advance_keeps_the_high_nibble);
    RUN_TEST(advance_touches_no_neighbouring_byte);
    RUN_TEST(advance_ignores_the_unit_index_argument);
    RUN_TEST(advance_run_twice_changes_nothing_more);
}
