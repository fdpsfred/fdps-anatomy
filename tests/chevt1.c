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
 *
 * The chapter 5 handler at 000370e0 is the same shape with one range instead of
 * four and a one-shot latch in front of it, and is covered the same way: its
 * two bounds, the inclusive compare between them and the 0xf0 mask are literals
 * in its instruction stream, and the latch is element 0x10 of the cell-event
 * flag array, which the cases below write before every call because it is a
 * ticket 23 symbol whose starting value nothing here may assume.
 *
 * The chapter 7 handler at 00037250 is the chapter 5 shape with the latch taken
 * away and the range 4..8 in place of 6..0x22, and is covered the same way: its
 * two bounds, the inclusive compare between them and the 0xf0 mask are literals
 * in its instruction stream, and the absence of a guard in front of its loop is
 * asserted by putting the latch slot up and watching it run anyway.
 *
 * The chapter 3 handler at 00036bb0 stages differently again and says why in
 * its own note: its payload is a call into fdps_deploy_wave, which opens
 * ICON.CEL and FIELD.VFS for itself, so the cases need those files and skip
 * themselves without them.
 *
 * The chapter 3 handler at 00036ea0 is the only one of the family that reads
 * the unit its argument names, so its cases stage two units with a side byte
 * each and call with the index of each in turn; everything else about its
 * staging is the same fixture, and its latch is element 0x11 rather than the
 * 0x10 the rest of the family shares.
 *
 * The chapter 3 handler at 00036f10 is the ambush's shape with the latch taken
 * away and a store to the battle-end global added behind the deployment, and it
 * uses the same fixture: the wave, the map number and the placement flag are
 * only readable off a real deployment, and the code it stores is read straight
 * back off the global.  Having no guard of any kind is asserted by putting both
 * of the family's latch slots up and watching it run anyway, and by calling it
 * twice.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---- fdps_chapter_05_event_enemies_advance, 000370e0 -------------------- */

/* The single inclusive range the one inline loop covers, read off the
   constants at 00037103 (6) and 0003710a (0x22) with the signed JLE at
   00037136. */
#define CH05_FIRST_INDEX 6
#define CH05_LAST_INDEX  0x22

/* Three records past the last index the handler writes, so an off-by-one at
   the top end of the range has somewhere visible to land. */
#define CH05_STAGE_UNITS 0x26

/* Element 0x10 of the cell-event flag array, the slot the CMP and MOV at
   000370f3 and 000370fc name as byte ptr [0x000640e8]. */
#define CH05_LATCH_SLOT 0x10

static struct fdps_unit_record ch05_units[CH05_STAGE_UNITS];

/* Give every record the same AI byte, point the array global at the block and
   put the latch in the state this case wants.  The staged AI value carries a
   high nibble as well as a behaviour code, because the whole point of the
   merge is that only one of the two moves.  What the flag array holds outside
   the latch slot is ticket 23's and is not asserted anywhere below. */
static void stage_ch05_units(int ai_behavior, int latch)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch05_units;
    for (i = 0; i < (int) sizeof(ch05_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH05_STAGE_UNITS; i++) {
        ch05_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch05_units;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] =
        (unsigned char) latch;
}

/* Exactly indices 6..0x22 are rewritten and every record either side of the
   range is left as it was.  The staged 0x52 is behaviour code 2 -- hold
   position, which is what map04.dat deploys these units in -- under a high
   nibble of 0x50; the range comes out 0x50 because the mode ORed in is 0, and
   the rest keep 0x52.  Indices 0..5 are the five party slots and the guest
   hero, which the range deliberately starts above, and 0x23..0x25 are past the
   last deployed unit: both ends have to be untouched, and 0x22 itself has to
   be written, which is what makes the JLE inclusive rather than a bound one
   short. */
static void ch05_advance_clears_exactly_the_range(void)
{
    int i;

    stage_ch05_units(0x52, 0);
    fdps_chapter_05_event_enemies_advance(0);

    for (i = 0; i < CH05_STAGE_UNITS; i++) {
        if (i >= CH05_FIRST_INDEX && i <= CH05_LAST_INDEX) {
            CHECK_EQ(ch05_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch05_units[i].ai_behavior, 0x52);
        }
    }
}

/* The high nibble is carried across untouched by the AND 0xf0 at 00037153 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere, so a merge that assigned the mode whole -- or that
   masked with anything wider -- would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch05_advance_keeps_the_high_nibble(void)
{
    stage_ch05_units(0, 0);
    ch05_units[6].ai_behavior = 0xc2;
    ch05_units[7].ai_behavior = 0x02;
    ch05_units[0x11].ai_behavior = 0xff;
    ch05_units[0x20].ai_behavior = 0x40;
    ch05_units[0x22].ai_behavior = 0x8b;

    fdps_chapter_05_event_enemies_advance(0);

    CHECK_EQ(ch05_units[6].ai_behavior, 0xc0);
    CHECK_EQ(ch05_units[7].ai_behavior, 0x00);
    CHECK_EQ(ch05_units[0x11].ai_behavior, 0xf0);
    CHECK_EQ(ch05_units[0x20].ai_behavior, 0x40);
    CHECK_EQ(ch05_units[0x22].ai_behavior, 0x80);
}

/* The latch is raised by the run, and the run is what raises it: the store at
   000370fc is inside the guarded body, so a handler that returned early leaves
   the slot alone.  Nothing else in the array moves -- the store names one byte
   -- so the neighbouring cell-event flags a map cell would set are asserted
   unchanged. */
static void ch05_advance_raises_the_latch(void)
{
    stage_ch05_units(0x52, 0);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1] = 0;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1] = 0;

    fdps_chapter_05_event_enemies_advance(0);

    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1], 0);
}

/* Once the latch is up the handler does nothing at all, which is what makes it
   one-shot: the trigger tile can be walked over again on a later turn, and the
   enemies that have since been put back into another behaviour mode by their
   own AI must not be pushed to mode 0 a second time.  The units are restaged
   to 0x52 between the two calls so a second run would be visible. */
static void ch05_advance_runs_once_per_chapter(void)
{
    stage_ch05_units(0x52, 0);
    fdps_chapter_05_event_enemies_advance(0);
    CHECK_EQ(ch05_units[6].ai_behavior, 0x50);

    stage_ch05_units(0x52, 1);
    fdps_chapter_05_event_enemies_advance(0);
    CHECK_EQ(ch05_units[6].ai_behavior, 0x52);
    CHECK_EQ(ch05_units[0x22].ai_behavior, 0x52);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
}

/* The guard compares against 0, not against 1, so any non-zero value in the
   slot blocks the body and the slot is not overwritten on the way out.  This
   is the case that would come out differently if the emitted C had tested for
   equality with 1. */
static void ch05_advance_latch_tests_against_zero(void)
{
    stage_ch05_units(0x52, 2);
    fdps_chapter_05_event_enemies_advance(0);

    CHECK_EQ(ch05_units[6].ai_behavior, 0x52);
    CHECK_EQ(ch05_units[0x22].ai_behavior, 0x52);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 2);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside
   fdps_get_unit_record and an error in it shows up furthest from the base. */
static void ch05_advance_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch05_units;
    for (i = 0; i < (int) sizeof(ch05_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] = 0;

    fdps_chapter_05_event_enemies_advance(0);

    CHECK_EQ(bytes[6 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[6 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[6 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[5 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x23 * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 000370ec, before the
   latch is even tested, and never read, so the index of the unit that walked
   onto the trigger tile cannot reach the result.  This handler is reached
   through the four movement-step routines, which latch the pending event with
   phase 0 and then let the turn driver push the acting unit's real index, so
   the argument is a live value here rather than the literal 0 the turn-event
   dispatcher passes.  Index 3 is asserted unchanged because it is one an
   argument-driven handler would have written. */
static void ch05_advance_ignores_the_unit_index_argument(void)
{
    stage_ch05_units(0x52, 0);
    fdps_chapter_05_event_enemies_advance(3);
    CHECK_EQ(ch05_units[3].ai_behavior, 0x52);
    CHECK_EQ(ch05_units[6].ai_behavior, 0x50);

    stage_ch05_units(0x52, 0);
    fdps_chapter_05_event_enemies_advance(-1);
    CHECK_EQ(ch05_units[0x22].ai_behavior, 0x50);
    CHECK_EQ(ch05_units[0x23].ai_behavior, 0x52);

    stage_ch05_units(0x52, 0);
    fdps_chapter_05_event_enemies_advance(30000);
    CHECK_EQ(ch05_units[6].ai_behavior, 0x50);
    CHECK_EQ(ch05_units[5].ai_behavior, 0x52);
}

/* ---- fdps_chapter_07_event_enemies_advance, 00037250 -------------------- */

/* The single inclusive range the one inline loop covers, read off the
   constants at 00037263 (4) and 0003726a (8) with the signed JLE at
   00037296. */
#define CH07_FIRST_INDEX 4
#define CH07_LAST_INDEX  8

/* Three records past the last index the handler writes, so an off-by-one at
   the top end of the range has somewhere visible to land. */
#define CH07_STAGE_UNITS 12

static struct fdps_unit_record ch07_units[CH07_STAGE_UNITS];

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch07_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch07_units;
    for (i = 0; i < (int) sizeof(ch07_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH07_STAGE_UNITS; i++) {
        ch07_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch07_units;
}

/* Exactly indices 4..8 are rewritten and every record either side of the range
   is left as it was.  The staged 0x52 is behaviour code 2 -- hold position,
   which is what Icon06.dat's second deploy opcode gives the champion -- under a
   high nibble of 0x50; the range comes out 0x50 because the mode ORed in is 0,
   and the rest keep 0x52.  Indices 0..3 are chapter 7's four party records,
   which the range deliberately starts above, and 9..11 are past the last
   deployed unit: both ends have to be untouched.  Index 8 itself has to be
   written, which is what makes the JLE inclusive rather than a bound one short,
   and it is the only one of the five whose mode the event really changes. */
static void ch07_advance_clears_exactly_the_range(void)
{
    int i;

    stage_ch07_units(0x52);
    fdps_chapter_07_event_enemies_advance(0);

    for (i = 0; i < CH07_STAGE_UNITS; i++) {
        if (i >= CH07_FIRST_INDEX && i <= CH07_LAST_INDEX) {
            CHECK_EQ(ch07_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch07_units[i].ai_behavior, 0x52);
        }
    }
}

/* The high nibble is carried across untouched by the AND 0xf0 at 000372b3 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere, so a merge that assigned the mode whole -- or that
   masked with anything wider -- would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch07_advance_keeps_the_high_nibble(void)
{
    stage_ch07_units(0);
    ch07_units[4].ai_behavior = 0xc2;
    ch07_units[5].ai_behavior = 0x02;
    ch07_units[6].ai_behavior = 0xff;
    ch07_units[7].ai_behavior = 0x40;
    ch07_units[8].ai_behavior = 0x8b;

    fdps_chapter_07_event_enemies_advance(0);

    CHECK_EQ(ch07_units[4].ai_behavior, 0xc0);
    CHECK_EQ(ch07_units[5].ai_behavior, 0x00);
    CHECK_EQ(ch07_units[6].ai_behavior, 0xf0);
    CHECK_EQ(ch07_units[7].ai_behavior, 0x40);
    CHECK_EQ(ch07_units[8].ai_behavior, 0x80);
}

/* Nothing guards the loop: the instruction after the argument-slot store at
   0003725c is the first of the three constant stores, with no compare between
   them, so unlike the chapter 5 handler this one has no one-shot latch and runs
   its loop every time it is called.  The latch slot is put up before the call
   and the range still moves; the slot is also asserted unchanged, because a
   handler that had grown a latch would have written it. */
static void ch07_advance_has_no_one_shot_latch(void)
{
    stage_ch07_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] = 1;

    fdps_chapter_07_event_enemies_advance(0);

    CHECK_EQ(ch07_units[4].ai_behavior, 0x50);
    CHECK_EQ(ch07_units[8].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);

    stage_ch07_units(0x52);
    fdps_chapter_07_event_enemies_advance(0);
    CHECK_EQ(ch07_units[4].ai_behavior, 0x50);
    CHECK_EQ(ch07_units[8].ai_behavior, 0x50);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside fdps_get_unit_record
   and an error in it shows up furthest from the base. */
static void ch07_advance_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch07_units;
    for (i = 0; i < (int) sizeof(ch07_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_07_event_enemies_advance(0);

    CHECK_EQ(bytes[4 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[4 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[4 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[8 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[8 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[3 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[9 * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 0003725c and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The turn-event dispatcher pushes a literal 0 at 0002e13e,
   which is the only path this slot is reached by in the shipped data; the cell
   search and the death-script runner push a real, unchecked unit index.  Index
   3 is asserted unchanged because it is one an argument-driven handler would
   have written. */
static void ch07_advance_ignores_the_unit_index_argument(void)
{
    stage_ch07_units(0x52);
    fdps_chapter_07_event_enemies_advance(3);
    CHECK_EQ(ch07_units[3].ai_behavior, 0x52);
    CHECK_EQ(ch07_units[4].ai_behavior, 0x50);

    stage_ch07_units(0x52);
    fdps_chapter_07_event_enemies_advance(-1);
    CHECK_EQ(ch07_units[8].ai_behavior, 0x50);
    CHECK_EQ(ch07_units[9].ai_behavior, 0x52);

    stage_ch07_units(0x52);
    fdps_chapter_07_event_enemies_advance(30000);
    CHECK_EQ(ch07_units[4].ai_behavior, 0x50);
    CHECK_EQ(ch07_units[0].ai_behavior, 0x52);
}

/* ---- fdps_chapter_03_event_deploy_wave_for_turn, 00036bb0 --------------- */

/* Chapter 3's turn-scheduled reinforcement.
 *
 * None of the three values the handler hands fdps_deploy_wave -- PUSH 0x2, the
 * counter less one, and a zeroed EAX, at 00036c1d..00036c27 -- is left anywhere
 * afterwards, so the only way to see any of them is to let the deployment
 * happen.  These cases run fdps_deploy_wave for real and read back what landed
 * where:
 *
 *   MAP02.COD  record 0 (24, 17)  record 1 (5, 4)
 *              record 2 (23, 16)  record 3 (25, 15)
 *   MAP00.COD  record 0 (18, 0)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the placement records inside the shipped FIELD.VFS, staged through
 * tests/gamefile.lst; the MAP00 and MAP01 pair are the ones tests/deploy.c and
 * the chapter 10 and 17 cases expect, and they are here only as the coordinates
 * a map number read from the chapter global instead of the literal 2 would land
 * on.  That function opens ICON.CEL and FIELD.VFS for itself and a run without
 * them would not fail a check, it would hang in fdps_wait_any_key, so every
 * case here skips itself when they are not there.
 *
 * Which wave the spawn table's records carry is staged rather than read from
 * map02.dat, because what is under test is the arithmetic that picks a wave and
 * not what chapter 3 happens to have tagged: a record is planted at each of the
 * waves either side of the ones the turns should ask for, so asking for the
 * wrong one is visible as a different character id on a different tile.
 *
 * WHICH TEXT ENTRY EACH DRAW ASKS FOR IS NOT ASSERTED ANYWHERE BELOW.  The
 * three draws take their whole effect through pixels at the VGA aperture --
 * fdps_draw_text writes to the address the handler hands it, 0xa0000, keeps no
 * state and returns a cursor this handler discards -- so a unit test has
 * nothing to read back.  The entry ids are literals in the instruction stream
 * (PUSH 0x13, PUSH 0x15, PUSH 0x14) and the reviewer's reading of them is what
 * stands behind the emitted C.  What the cases below do pin about the draws is
 * that neither of them stops the deployment: turns 3 and 14, the two that
 * speak, deploy their wave like any other.
 *
 * So the chapter's text block is staged as a fixture whose every entry is a
 * lone -1 terminator.  fdps_draw_text walks it, draws nothing, touches no
 * global and returns at once, which is what keeps a case on turn 3 or turn 14
 * from painting the screen and standing a modal wait on a keyboard nothing is
 * typing at.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* What the handler takes off the turn counter: DEC EAX at 00036c25.  Turns are
   written here as turn numbers and the wave each should reach is derived from
   them, so a test that agreed with a wrong offset would have to disagree with
   the turns map02.dat schedules. */
#define CH03_WAVE_TURN_OFFSET 1

/* The first and the last of the twelve turns map02.dat's turn-event table names
   for this slot, {3, 0, 0} through {14, 0, 0}.  They are also the two the
   handler compares against, CMP 0x3 at 00036bc3 and CMP 0xe at 00036bf1, so
   they are the turns on which a draw happens as well as a deployment. */
#define CH03_FIRST_WAVE_TURN 3
#define CH03_LAST_WAVE_TURN 0xe

/* Two scheduled turns that speak nothing, either side of the first one that
   does, and a turn the counter never legitimately holds. */
#define CH03_QUIET_TURN 2
#define CH03_EARLIEST_TURN 1
#define CH03_ZERO_TURN 0

/* A turn past the last one the map schedules.  Nothing in the shipped data
   reaches it; it is here because the handler has no upper bound either. */
#define CH03_UNSCHEDULED_TURN 20

/* A map big enough for MAP02.COD's records, which reach (25, 17), with a row
   below the lowest of them for the nearest-free-tile case to land on.  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH03_GRID_W 32
#define CH03_GRID_H 24

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH03_SPAWN_TABLE_RECORD_BASE 0x83
#define CH03_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile map's
   width word at +7 with its 16-bit ids from +0xb, the attribute table's 4-byte
   rows from +0x11, and the event layer's width at +7 with its cells from +0x10
   (src/maptile.c). */
#define CH03_TILE_MAP_WIDTH_OFFSET 7
#define CH03_TILE_MAP_IDS_OFFSET 0xb
#define CH03_TILE_ATTR_ROWS_OFFSET 0x11
#define CH03_EVENT_LAYER_WIDTH_OFFSET 7
#define CH03_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH03_TERRAIN_WALKABLE 1
#define CH03_TERRAIN_BLOCKED 5

#define CH03_TILE_ATTR_ROWS 16
#define CH03_CHAR_TABLE_ROWS 8
#define CH03_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH03_ITEM_TABLE_ROWS 256
#define CH03_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH03_UNIT_STRIDE 0x50

/* The five staged deployment records and the wave each is tagged with: one at
   each of the waves turns 1, 2, 3 and 14 reach, and one tagged 0xff.  The
   character ids are arbitrary and only have to differ, so that which record was
   deployed is readable off the unit as well as off the tile it landed on.

   The 0xff record is the witness for contract C.  A record's wave byte is a
   single unsigned byte compared against the int the handler computed, so a
   negative key matches nothing; read as a signed char it would be -1 and the
   turn-0 case would deploy it. */
#define CH03_WAVE0_RECORD 0
#define CH03_WAVE1_RECORD 1
#define CH03_WAVE2_RECORD 2
#define CH03_WAVE13_RECORD 3
#define CH03_WAVE_FF_RECORD 4
#define CH03_WAVE0_CHAR_ID 5
#define CH03_WAVE1_CHAR_ID 6
#define CH03_WAVE2_CHAR_ID 7
#define CH03_WAVE13_CHAR_ID 9
#define CH03_WAVE_FF_CHAR_ID 10
#define CH03_SPAWN_RECORD_COUNT 5

/* The chapter text block every draw in this file is pointed at.  0x18 entries
   covers every id the four chapter 3 handlers name -- 0x13, 0x14 and 0x15 from
   the turn-scheduled one, 0x12 from the ambush, 0x16 from the tile-triggered
   reinforcement and 0x17 from the turn-limit defeat -- and every one of them
   holds the same offset, so whichever entry a draw resolves it lands on the lone
   -1 that follows the table and the stream ends before a glyph is drawn.  The
   offset is added to the block's own base and not to the slot it was read from
   (src/text.c). */
#define CH03_TEXT_ENTRY_COUNT 0x18

static unsigned char ch03_grid[4 + CH03_GRID_W * CH03_GRID_H * 2];
static unsigned char ch03_spawn_table[CH03_SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char ch03_tile_map[CH03_TILE_MAP_IDS_OFFSET +
                                   CH03_GRID_W * CH03_GRID_H * 2];
static unsigned char ch03_tile_attr[CH03_TILE_ATTR_ROWS_OFFSET +
                                    CH03_TILE_ATTR_ROWS * 4];
static unsigned char ch03_event_layer[CH03_EVENT_LAYER_CELLS_OFFSET +
                                      CH03_GRID_W * CH03_GRID_H];
static struct fdps_character_base_record ch03_char_base[CH03_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch03_growth[CH03_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch03_enemy[CH03_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch03_items[CH03_ITEM_TABLE_ROWS];
static short ch03_text_block[CH03_TEXT_ENTRY_COUNT + 1];

static int ch03_files_checked = 0;
static int ch03_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch03_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch03_files_checked) {
        return;
    }
    ch03_files_checked = 1;

    fp = fopen("ICON.CEL", "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    if (size < (long) (15 + 0x2970)) {
        return;
    }

    fp = fopen("FIELD.VFS", "rb");
    if (fp == NULL) {
        return;
    }
    fclose(fp);
    ch03_files_ready = 1;
}

static void ch03_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch03_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch03_spawn_table + CH03_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch03_set_spawn(int index, int char_id, int wave_no)
{
    ch03_spawn_at(index)->char_id = (unsigned char) char_id;
    ch03_spawn_at(index)->level = 1;
    ch03_spawn_at(index)->side = 2;
    ch03_spawn_at(index)->equipped_item_0 = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->equipped_item_1 = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[0] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[1] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[2] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[3] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[4] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->carried_items[5] = CH03_ITEM_ID_NONE;
    ch03_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch03_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch03_tile_attr + CH03_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch03_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch03_tile_map + CH03_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH03_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch03_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH03_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map
   0, the turn counter on the given turn and the chapter text block pointed at
   the empty-stream fixture, plus five deployment records tagged waves 0, 1, 2,
   13 and 0xff at table indices 0 to 4.  Placement record and table index are the
   same number, so which record was deployed is readable twice over: off the
   character id and off the tile it landed on.

   The unit already on the map stands at (0, 0), clear of every placement record
   these cases read back. */
static void ch03_stage(int battle_turn)
{
    int i;

    ch03_zero_bytes(ch03_grid, (int) sizeof(ch03_grid));
    ch03_zero_bytes(ch03_spawn_table, (int) sizeof(ch03_spawn_table));
    ch03_zero_bytes(ch03_tile_map, (int) sizeof(ch03_tile_map));
    ch03_zero_bytes(ch03_tile_attr, (int) sizeof(ch03_tile_attr));
    ch03_zero_bytes(ch03_event_layer, (int) sizeof(ch03_event_layer));
    ch03_zero_bytes(ch03_char_base, (int) sizeof(ch03_char_base));
    ch03_zero_bytes(ch03_growth, (int) sizeof(ch03_growth));
    ch03_zero_bytes(ch03_enemy, (int) sizeof(ch03_enemy));
    ch03_zero_bytes(ch03_items, (int) sizeof(ch03_items));

    *(short *) ch03_grid = (short) CH03_GRID_W;
    *(short *) (ch03_grid + 2) = (short) CH03_GRID_H;

    *(short *) (ch03_tile_map + CH03_TILE_MAP_WIDTH_OFFSET) =
        (short) CH03_GRID_W;
    for (i = 0; i < CH03_TILE_ATTR_ROWS; i++) {
        ch03_set_terrain(i, CH03_TERRAIN_WALKABLE);
    }

    *(short *) (ch03_event_layer + CH03_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH03_GRID_W;

    for (i = 0; i < CH03_TEXT_ENTRY_COUNT; i++) {
        ch03_text_block[i] = (short) (CH03_TEXT_ENTRY_COUNT * 2);
    }
    ch03_text_block[CH03_TEXT_ENTRY_COUNT] = -1;

    data_fdps_battle_move_grid_ptr = ch03_grid;
    data_fdps_tile_event_data_table_ptr = ch03_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch03_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch03_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch03_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch03_char_base;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) ch03_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch03_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch03_items;
    data_fdps_current_chapter_text_ptr = (unsigned char *) ch03_text_block;

    data_fdps_map_unit_array_ptr = (unsigned char *) malloc(CH03_UNIT_STRIDE);
    ch03_zero_bytes(data_fdps_map_unit_array_ptr, CH03_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;

    ch03_spawn_table[CH03_SPAWN_TABLE_COUNT_OFFSET] = CH03_SPAWN_RECORD_COUNT;
    ch03_set_spawn(CH03_WAVE0_RECORD, CH03_WAVE0_CHAR_ID, 0);
    ch03_set_spawn(CH03_WAVE1_RECORD, CH03_WAVE1_CHAR_ID, 1);
    ch03_set_spawn(CH03_WAVE2_RECORD, CH03_WAVE2_CHAR_ID, 2);
    ch03_set_spawn(CH03_WAVE13_RECORD, CH03_WAVE13_CHAR_ID,
                   CH03_LAST_WAVE_TURN - CH03_WAVE_TURN_OFFSET);
    ch03_set_spawn(CH03_WAVE_FF_RECORD, CH03_WAVE_FF_CHAR_ID, 0xff);

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = battle_turn;
}

/* The fields the cases below read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void ch03_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH03_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 8);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave asked for is the turn counter less one.  Turn 2 brings on the wave-1
   record, table index 1, which lands on MAP02.COD record 1 at (5, 4), and turn 1
   the wave-0 record, index 0 at (24, 17).  An offset of 0 or 2 would deploy one
   of the neighbours instead, and both the character id and the tile would say
   so. */
static void ch03_wave_is_the_turn_counter_less_one(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_QUIET_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 4);

    ch03_stage(CH03_EARLIEST_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 24);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 17);
}

/* The deployment is not inside either speech branch: turn 3 draws entry 0x13
   before it and entry 0x14 after it and still brings on the wave-2 record,
   table index 2 at MAP02.COD record 2 (23, 16), and turn 14 draws entry 0x15
   and still brings on the wave-13 record, index 3 at (25, 15).  A handler that
   had the deployment inside the first if-block, or that returned after
   speaking, would leave the map with one unit on it. */
static void ch03_the_speaking_turns_still_deploy(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_FIRST_WAVE_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 23);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 16);

    ch03_stage(CH03_LAST_WAVE_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE13_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 25);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 15);
}

/* The map the wave is placed under is the literal 2 pushed at 00036c27 and not
   data_fdps_chapter_current_chapter_id, which is what the chapter 10, 17 and 18
   handlers push at the same argument.  The same wave-0 record lands on
   MAP02.COD's record 0 at (24, 17) with that global on 0 and again with it on 1
   -- MAP00.COD's record 0 is (18, 0) and MAP01.COD's is (9, 4), so a handler
   that read the global would land somewhere else in at least one of the two. */
static void ch03_map_number_is_the_literal_two(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_EARLIEST_TURN);
    data_fdps_chapter_current_chapter_id = 0;
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 24);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 17);

    ch03_stage(CH03_EARLIEST_TURN);
    data_fdps_chapter_current_chapter_id = 1;
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 24);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 17);
}

/* The key is not clamped at the bottom, and it is compared against an unsigned
   byte: a turn counter of 0 asks for wave -1, which matches none of the five
   staged records -- not the wave-0 one a clamp would reach, and not the 0xff one
   a signed read of the record's wave byte would turn into -1.  The file open and
   load still happen and nothing is deployed. */
static void ch03_turn_zero_deploys_nothing(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_ZERO_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ((int) ch03_unit(0)->char_id, 0);
}

/* The key is unbounded at the top too, and the subtraction is a subtraction
   rather than a choice between the twelve turns the map schedules: turn 20 is
   not in map02.dat's turn table at all and asks for wave 19, which no staged
   record carries, so the walk matches nothing while the file open and load still
   happen. */
static void ch03_unscheduled_turn_asks_for_its_own_wave(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_UNSCHEDULED_TURN);
    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00036c1d -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP02.COD record 1 names (5, 4);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search, and the unit lands one tile away.  A flag of 1 would drop it on
   (5, 4) regardless of the terrain there.

   (5, 5) is which of the four tiles at distance 1 it lands on, because the scan
   is row-major over the whole grid and a tie is accepted (CMP EAX, [EBP-0x14] /
   JLE at 000233e6), so the last candidate at the best distance wins. */
static void ch03_places_on_the_nearest_free_tile(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_QUIET_TURN);
    ch03_set_tile_id(5, 4, 1);
    ch03_set_terrain(1, CH03_TERRAIN_BLOCKED);

    fdps_chapter_03_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 5);
}

/* Nothing guards the deployment: there is no latch anywhere in the body, so a
   second firing on the same turn deploys the same wave again rather than being
   refused.  The slot the one-shot handlers of this family latch is put up
   beforehand and the wave still arrives, and the slot is asserted unchanged
   because a handler that had grown a latch would have written it. */
static void ch03_has_no_one_shot_latch(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_stage(CH03_QUIET_TURN);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] = 1;

    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);

    fdps_chapter_03_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 00036bbc before the turn
   counter is read, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for or the placement flag.  The
   turn-event runner is the only path this slot is reached by in the shipped data
   and it pushes a literal 0; the values put through below are that 0, two real
   unit indices of the kind the tile, cell-search and death-script dispatchers of
   the same table forward, and two that are not indices at all. */
static void ch03_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 7, -1, 30000};
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch03_stage(CH03_QUIET_TURN);
        fdps_chapter_03_event_deploy_wave_for_turn(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
        CHECK_EQ((int) ch03_unit(1)->pos_y, 4);
    }
}

/* ---- fdps_chapter_03_event_deploy_wave_1, 00036c70 --------------------- */

/* Chapter 3's ambush.
 *
 * It is staged through the same fixture as the turn-scheduled handler above,
 * for the same reason: neither of the three values it hands fdps_deploy_wave --
 * PUSH 0x2, PUSH 0x1 and a zeroed EAX at 00036cb6..00036cbb -- is left anywhere
 * afterwards, so the only way to see any of them is to let the deployment
 * happen against the real MAP02.COD inside FIELD.VFS.  ch03_stage's wave-1
 * record is table index 1 with character id CH03_WAVE1_CHAR_ID, and MAP02.COD's
 * record 1 is (5, 4); the records tagged waves 0, 2, 13 and 0xff either side of
 * it land on different tiles with different ids, so an ambush that asked for
 * the wrong wave is visible twice over.
 *
 * The cases that get past the latch need ICON.CEL and FIELD.VFS and skip
 * themselves without them, exactly as the ones above do.  The two that do not
 * get past it are not gated: a latched call returns before either CALL, so it
 * opens nothing.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED ANYWHERE BELOW, for the
 * reason the section above gives -- fdps_draw_text takes its whole effect
 * through pixels at the VGA aperture and keeps no state -- so PUSH 0x12 at
 * 00036ca6 stands on the reviewer's reading of the instruction stream.  What
 * the cases do pin about the draw is that it does not stop the deployment and
 * that it is inside the latch rather than in front of it.  ch03_stage's text
 * block is the same empty-stream fixture, every entry resolving to a lone -1,
 * so a draw paints nothing and cannot stand a modal wait on a keyboard nothing
 * is typing at.
 */

/* Five turn counters spanning everything the sibling handler's DEC EAX cares
   about -- the two it compares against, one either side, and one past the last
   turn map02.dat schedules.  This handler has no CMP against
   data_fdps_battle_turn_counter anywhere in its 0x5a bytes, so all five have to
   deploy the same wave. */
#define CH03_AMBUSH_TURN_COUNT 5

/* Put the latch down and every neighbouring flag with it, so a case that reads
   one of them back is reading what it wrote.  ch03_stage does not touch the
   flag array -- it is a ticket 23 symbol whose starting value nothing here may
   assume -- so the latch is always set explicitly before a call. */
static void ch03_ambush_stage(int battle_turn, int latch)
{
    ch03_stage(battle_turn);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1] = 0;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] =
        (unsigned char) latch;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1] = 0;
}

/* The wave brought on is the literal 1: the wave-1 record, table index 1, lands
   on MAP02.COD's record 1 at (5, 4) carrying CH03_WAVE1_CHAR_ID.  The wave-0
   record would land on (24, 17) with id CH03_WAVE0_CHAR_ID and the wave-2
   record on (23, 16) with id CH03_WAVE2_CHAR_ID, so a handler that had copied
   the sibling's turn arithmetic, or that had read the counter at all, would
   land somewhere else with something else on it. */
static void ch03_ambush_deploys_wave_one(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    fdps_chapter_03_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 4);
}

/* The latch is raised by the run, and it is element 0x10 and no other: the CMP
   and the MOV at 00036c83 and 00036c8c name byte ptr [0x000640e8], one byte
   inside the 32-byte array based at 0x000640d8.  Both neighbours are put up
   before the call and neither blocks the body, which is what pins the index --
   a handler reading 0x0f or 0x11 instead would have returned at once -- and
   neither is written, which is what pins the width of the store to one byte. */
static void ch03_ambush_raises_only_its_own_latch_slot(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1] = 1;
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1] = 1;

    fdps_chapter_03_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1], 1);
}

/* Once the latch is up the handler does nothing at all, which is what makes the
   ambush one-shot: the trigger region is 24 cells wide and a unit can walk back
   onto it every turn for the rest of the chapter.  The second call is made with
   the array left exactly as the first call left it, so anything the handler did
   twice would show as a third unit. */
static void ch03_ambush_runs_once_per_chapter(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    fdps_chapter_03_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);

    fdps_chapter_03_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
}

/* The guard compares against 0, not against 1, so any non-zero value in the
   slot blocks the body and the slot is not overwritten on the way out.  This is
   the case that would come out differently if the emitted C had tested for
   equality with 1.  It is not gated on the game files: a blocked call returns
   before either CALL and opens nothing, which is itself part of what is being
   asserted. */
static void ch03_ambush_latch_tests_against_zero(void)
{
    ch03_ambush_stage(CH03_QUIET_TURN, 2);
    fdps_chapter_03_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1], 0);
}

/* Both CALLs are inside the guard: the JNZ at 00036c8a jumps past the draw as
   well as the deployment, straight to the epilogue.  With the latch on the
   value the handler's own successful run leaves behind, nothing is deployed,
   the unit already on the map is untouched and the latch is not rewritten.
   Not gated, for the same reason as the case above. */
static void ch03_ambush_blocked_call_does_nothing(void)
{
    ch03_ambush_stage(CH03_QUIET_TURN, 1);
    fdps_chapter_03_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 1);
    CHECK_EQ((int) ch03_unit(0)->char_id, 0);
    CHECK_EQ((int) ch03_unit(0)->pos_x, 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT - 1], 0);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT + 1], 0);
}

/* Nothing in the body reads data_fdps_battle_turn_counter -- there is no CMP
   against it in the 0x5a bytes -- so the wave brought on is the same on every
   turn.  The five staged here are the two the sibling handler compares against,
   the turn either side of the first of them, the turn the counter is reset to,
   and one past the last turn map02.dat schedules; a handler carrying the
   sibling's DEC EAX would deploy a different record on four of the five and
   nothing at all on the turn-0 one. */
static void ch03_ambush_ignores_the_turn_counter(void)
{
    static int turns[CH03_AMBUSH_TURN_COUNT] = {CH03_ZERO_TURN,
                                                CH03_EARLIEST_TURN,
                                                CH03_QUIET_TURN,
                                                CH03_FIRST_WAVE_TURN,
                                                CH03_UNSCHEDULED_TURN};
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < CH03_AMBUSH_TURN_COUNT; i++) {
        ch03_ambush_stage(turns[i], 0);
        fdps_chapter_03_event_deploy_wave_1(0);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
        CHECK_EQ((int) ch03_unit(1)->pos_y, 4);
    }
}

/* The map the wave is placed under is the literal 2 pushed at 00036cbb and not
   data_fdps_chapter_current_chapter_id, which is what the chapter 10, 17 and 18
   handlers push at the same argument.  The same wave-1 record lands on
   MAP02.COD's record 1 at (5, 4) with that global on 0 and again with it on 1 --
   MAP00.COD's record 0 is (18, 0) and MAP01.COD's is (9, 4), so a handler that
   read the global would land somewhere else in at least one of the two. */
static void ch03_ambush_map_number_is_the_literal_two(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    data_fdps_chapter_current_chapter_id = 0;
    fdps_chapter_03_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 4);

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    data_fdps_chapter_current_chapter_id = 1;
    fdps_chapter_03_event_deploy_wave_1(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00036cb6 -- so the
   ambushers are put on the nearest free walkable tile to their placement record
   rather than on the record's own tile.  MAP02.COD record 1 names (5, 4);
   giving that one cell a tile id whose attribute row is terrain 5 takes it out
   of the search and the unit lands one tile away.  A flag of 1 would drop it on
   (5, 4) regardless of the terrain there.

   (5, 5) is which of the four tiles at distance 1 it lands on, because the scan
   is row-major over the whole grid and a tie is accepted, so the last candidate
   at the best distance wins. */
static void ch03_ambush_places_on_the_nearest_free_tile(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_ambush_stage(CH03_QUIET_TURN, 0);
    ch03_set_tile_id(5, 4, 1);
    ch03_set_terrain(1, CH03_TERRAIN_BLOCKED);

    fdps_chapter_03_event_deploy_wave_1(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
    CHECK_EQ((int) ch03_unit(1)->pos_y, 5);
}

/* The incoming argument slot is overwritten with 0 at 00036c7c, before the
   latch is even tested, and never read back, so the index of the unit that
   walked onto the trigger tile cannot reach the wave asked for, the map asked
   for or the placement flag.  This slot is reached through the four
   movement-step routines, which report occasion 0 and then let the turn driver
   push the acting unit's real index, so the argument is a live value here
   rather than the literal 0 the turn-event dispatcher passes; the values put
   through below are that 0, two real unit indices, and two that are not indices
   at all. */
static void ch03_ambush_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 7, -1, 30000};
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch03_ambush_stage(CH03_QUIET_TURN, 0);
        fdps_chapter_03_event_deploy_wave_1(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch03_unit(1)->pos_x, 5);
        CHECK_EQ((int) ch03_unit(1)->pos_y, 4);
        CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
    }
}

/* ---- fdps_chapter_03_event_deploy_wave_14, 00036ea0 -------------------- */

/* Chapter 3's tile-triggered reinforcement.
 *
 * It is staged through the same fixture as the two handlers above, and for the
 * same reason: neither the wave, the map number nor the placement flag it hands
 * fdps_deploy_wave is left anywhere afterwards, so the only way to read any of
 * them back is to let the deployment happen against the real MAP02.COD inside
 * FIELD.VFS.  Two deployment records are added to the five ch03_stage plants,
 * at table indices 5 and 6 and tagged waves 14 and 15, so the wave either side
 * of the one asked for is present as well: MAP02.COD record 5 is (25, 13) and
 * record 6 is (22, 15), and the five records ch03_stage leaves at indices 0 to
 * 4 cover waves 0, 1, 2, 13 and 0xff on tiles of their own.
 *
 * What separates this handler from the ambush above is the record lookup in
 * front of the latch, so the unit array is staged with two units rather than
 * one, each with its own side byte, and the cases below call with the index of
 * each in turn.  A deployment reallocs that array, so it is malloc'd and never
 * freed, exactly as ch03_stage's own is.
 *
 * The cases that get past both tests need ICON.CEL and FIELD.VFS and skip
 * themselves without them.  The ones that do not get past them are not gated: a
 * refused call returns before either CALL, so it opens nothing.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * sections above give -- fdps_draw_text takes its whole effect through pixels
 * at the VGA aperture and keeps no state -- so PUSH 0x16 at 00036ee9 stands on
 * the reviewer's reading of the instruction stream.  What the cases do pin
 * about the draw is that it does not stop the deployment and that it is inside
 * the guard rather than in front of it.
 */

/* The latch this handler tests and raises: byte ptr [0x000640e9] at 00036ebb
   and 00036ecf, element 0x11 of the 32-byte flag array based at 0x000640d8 --
   one past CH05_LATCH_SLOT, which is the slot every other one-shot handler of
   the family shares. */
#define CH03_REINFORCE_LATCH_SLOT 0x11

/* The wave the handler asks for: PUSH 0xe at 00036efc. */
#define CH03_REINFORCE_WAVE 0xe

/* The two records added on top of ch03_stage's five, and the character ids that
   tell them apart from those five and from each other.  Table index and
   placement record are the same number, so which one was deployed is readable
   twice over: off the character id and off the tile it landed on. */
#define CH03_WAVE14_RECORD 5
#define CH03_WAVE15_RECORD 6
#define CH03_WAVE14_CHAR_ID 11
#define CH03_WAVE15_CHAR_ID 12
#define CH03_REINFORCE_SPAWN_COUNT 7

/* MAP02.COD's placement records 5 and 6, the tiles the two added deployment
   records land on.  MAP00.COD's record 5 is (14, 6) and MAP01.COD's is (9, 5),
   which is what makes the map number readable off the tile. */
#define CH03_WAVE14_TILE_X 25
#define CH03_WAVE14_TILE_Y 13
#define CH03_WAVE15_TILE_X 22
#define CH03_WAVE15_TILE_Y 15

/* The three side codes the handler's test sorts: 0 is the enemy, which the test
   keeps out, and 1 the guest and 2 the player's roster, which both spring it.
   0xff is not a side any deployment writes; it is here because the compare is
   against 0 over the whole unsigned byte, so the top of the range has to behave
   like the middle of it. */
#define SIDE_ENEMY 0
#define SIDE_GUEST 1
#define SIDE_PLAYER 2
#define SIDE_HIGH_BIT_SET 0xff

/* Two units on the map, each with its own side byte, so a case can ask which
   record the handler read; the latch slot is written along with both its
   neighbours, because it is a ticket 23 symbol whose starting value nothing
   here may assume.  The turn counter is left on a quiet turn: this handler has
   no CMP against data_fdps_battle_turn_counter anywhere in its 0x6f bytes.

   The two units stand at (0, 0) and (1, 0), clear of every placement record
   these cases read back. */
static void ch03_reinforce_stage(int latch, int side_0, int side_1)
{
    ch03_stage(CH03_QUIET_TURN);

    ch03_set_spawn(CH03_WAVE14_RECORD, CH03_WAVE14_CHAR_ID,
                   CH03_REINFORCE_WAVE);
    ch03_set_spawn(CH03_WAVE15_RECORD, CH03_WAVE15_CHAR_ID,
                   CH03_REINFORCE_WAVE + 1);
    ch03_spawn_table[CH03_SPAWN_TABLE_COUNT_OFFSET] =
        CH03_REINFORCE_SPAWN_COUNT;

    data_fdps_map_unit_array_ptr =
        (unsigned char *) malloc(2 * CH03_UNIT_STRIDE);
    ch03_zero_bytes(data_fdps_map_unit_array_ptr, 2 * CH03_UNIT_STRIDE);
    data_fdps_map_unit_count = 2;
    ch03_unit(0)->side = (unsigned char) side_0;
    ch03_unit(1)->side = (unsigned char) side_1;
    ch03_unit(1)->pos_x = 1;

    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT - 1] = 0;
    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT] =
        (unsigned char) latch;
    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT + 1] = 0;
}

/* The byte the handler reads off the record it looked up is the side at
   offset 6 -- CMP byte ptr [EAX+0x6],0x0 at 00036ec7 -- and the record it
   indexes is 0x50 bytes wide.  Both would agree with themselves while
   addressing another field if the layout were wrong. */
static void ch03_reinforce_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH03_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave brought on is the literal 14: the wave-14 record, table index 5,
   lands on MAP02.COD record 5 at (25, 13) carrying CH03_WAVE14_CHAR_ID.  The
   wave-15 record next to it would land on (22, 15) and the wave-13 record
   ch03_stage plants at index 3 on (25, 15), so a handler that asked for either
   neighbour would be visible twice over -- on the tile and on the id. */
static void ch03_reinforce_deploys_wave_fourteen(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    fdps_chapter_03_event_deploy_wave_14(0);

    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE14_CHAR_ID);
    CHECK_EQ((int) ch03_unit(2)->pos_x, CH03_WAVE14_TILE_X);
    CHECK_EQ((int) ch03_unit(2)->pos_y, CH03_WAVE14_TILE_Y);
}

/* A unit on side 0 does not fire it: the JNZ at 00036ecb falls through to the
   epilogue, so nothing is drawn, nothing is deployed and -- the part that
   matters for the next unit onto the tile -- the latch is left down.  A handler
   that raised the latch before the side test would have disarmed the event for
   the rest of the chapter the first time an enemy walked over it.  Not gated on
   the game files, because the refused call opens none. */
static void ch03_reinforce_ignores_a_unit_on_side_zero(void)
{
    ch03_reinforce_stage(0, SIDE_ENEMY, SIDE_PLAYER);
    fdps_chapter_03_event_deploy_wave_14(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 0);
}

/* The side test is a test against 0 and not a test for one particular side:
   the guest side 1, the player's own side 2 and a byte with its top bit set all
   get through it.  0xff is the one that separates the unsigned compare the
   assembly makes from a signed one -- read as a signed char it is -1, which a
   "greater than 0" test would refuse. */
static void ch03_reinforce_fires_on_every_non_zero_side(void)
{
    static int sides[3] = {SIDE_GUEST, SIDE_PLAYER, SIDE_HIGH_BIT_SET};
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < 3; i++) {
        ch03_reinforce_stage(0, sides[i], SIDE_ENEMY);
        fdps_chapter_03_event_deploy_wave_14(0);

        CHECK_EQ(data_fdps_map_unit_count, 3);
        CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE14_CHAR_ID);
        CHECK_EQ((int) ch03_unit(2)->pos_x, CH03_WAVE14_TILE_X);
    }
}

/* The side read is the side of the unit the argument names, not of a fixed one:
   fdps_get_unit_record is called with the incoming slot at 00036eac and the
   compare is made on what it hands back.  With side 0 on unit 0 and side 2 on
   unit 1, index 1 fires the event and index 0 refuses it, and swapping which
   unit carries which side swaps which index fires.  A handler that had ignored
   the argument -- the way the two chapter 3 handlers above do, both of which
   overwrite the slot with 0 first -- would behave the same for both. */
static void ch03_reinforce_reads_the_side_of_the_named_unit(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_ENEMY, SIDE_PLAYER);
    fdps_chapter_03_event_deploy_wave_14(1);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE14_CHAR_ID);

    ch03_reinforce_stage(0, SIDE_ENEMY, SIDE_PLAYER);
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 0);

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    fdps_chapter_03_event_deploy_wave_14(1);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 0);
}

/* A latch already up refuses the body whatever the side byte says, and it is
   tested against 0 rather than against 1 -- CMP byte ptr [0x000640e9],0x0 /
   JNZ at 00036ebb -- so a slot holding 2 blocks it just as a slot holding 1
   does.  Neither call is reached, so this one is not gated on the game files
   either. */
static void ch03_reinforce_is_blocked_by_a_raised_latch(void)
{
    ch03_reinforce_stage(1, SIDE_PLAYER, SIDE_ENEMY);
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 1);

    ch03_reinforce_stage(2, SIDE_PLAYER, SIDE_ENEMY);
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 2);
}

/* The slot latched is element 0x11 and no other, which is the one thing about
   this handler that could not be copied from the ambush above: both events are
   live on map02 at the same time -- its tile-event table sends cell code 1 to
   the ambush and code 2 to this handler -- so a shared byte would let whichever
   fired first suppress the other.  Both neighbours, 0x10 and 0x12, are put up
   before the call and neither blocks the body, which is what pins the index,
   and neither is written, which pins the width of the store to one byte. */
static void ch03_reinforce_raises_only_its_own_latch_slot(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT - 1] = 1;
    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT + 1] = 1;

    fdps_chapter_03_event_deploy_wave_14(0);

    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT - 1], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT + 1], 1);
}

/* Once the latch is up the handler does nothing at all, which is what makes the
   reinforcement one-shot: the trigger region can be walked over again on every
   turn that follows.  The second call is made with the array left exactly as
   the first call left it, so a second deployment would show as a fourth unit. */
static void ch03_reinforce_runs_once_per_chapter(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);

    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 1);
}

/* The map the wave is placed under is the literal 2 pushed at 00036efe and not
   data_fdps_chapter_current_chapter_id, which the chapter 10, 17 and 18
   handlers push at the same argument.  The wave-14 record lands on MAP02.COD's
   record 5 at (25, 13) with that global on 0 and again with it on 1 --
   MAP00.COD's record 5 is (14, 6) and MAP01.COD's is (9, 5), so a handler that
   read the global would land somewhere else in both. */
static void ch03_reinforce_map_number_is_the_literal_two(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    data_fdps_chapter_current_chapter_id = 0;
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->pos_x, CH03_WAVE14_TILE_X);
    CHECK_EQ((int) ch03_unit(2)->pos_y, CH03_WAVE14_TILE_Y);

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    data_fdps_chapter_current_chapter_id = 1;
    fdps_chapter_03_event_deploy_wave_14(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->pos_x, CH03_WAVE14_TILE_X);
    CHECK_EQ((int) ch03_unit(2)->pos_y, CH03_WAVE14_TILE_Y);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00036ef9 -- so the
   arrivals are put on the nearest free walkable tile to their placement record
   rather than on the record's own tile.  Taking (25, 13) out of the search by
   giving that one cell a tile id whose attribute row is terrain 5 moves the
   unit one tile down: the scan is row-major over the whole grid and a tie is
   accepted, so the last candidate at the best distance wins.  A flag of 1 would
   drop it on (25, 13) regardless of the terrain there. */
static void ch03_reinforce_places_on_the_nearest_free_tile(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_reinforce_stage(0, SIDE_PLAYER, SIDE_ENEMY);
    ch03_set_tile_id(CH03_WAVE14_TILE_X, CH03_WAVE14_TILE_Y, 1);
    ch03_set_terrain(1, CH03_TERRAIN_BLOCKED);

    fdps_chapter_03_event_deploy_wave_14(0);

    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE14_CHAR_ID);
    CHECK_EQ((int) ch03_unit(2)->pos_x, CH03_WAVE14_TILE_X);
    CHECK_EQ((int) ch03_unit(2)->pos_y, CH03_WAVE14_TILE_Y + 1);
}

/* ---- fdps_chapter_03_event_turn_limit_game_over, 00036f10 -------------- */

/* Chapter 3's turn-limit defeat.
 *
 * It is staged through the same fixture as the three handlers above and for the
 * same reason: neither the wave, the map number nor the placement flag it hands
 * fdps_deploy_wave is left anywhere afterwards, so the only way to read any of
 * them back is to let the deployment happen against the real MAP02.COD inside
 * FIELD.VFS.  Three deployment records are added to ch03_stage's five, at table
 * indices 5, 6 and 7 and tagged waves 14, 15 and 16, so the wave either side of
 * the one asked for is present as well: MAP02.COD record 5 is (25, 13) and
 * record 6 is (22, 15), and the five records ch03_stage leaves at indices 0 to 4
 * cover waves 0, 1, 2, 13 and 0xff on tiles of their own.  A handler that asked
 * for 14 or 16 instead of 15 lands a different character id on a different tile,
 * or nothing at all.
 *
 * The battle-end code is written before every call rather than read as it is
 * found: it is a ticket 23 symbol the build links zero-filled, and the cases
 * above it in this file leave their own values in it.
 *
 * Every case here needs ICON.CEL and FIELD.VFS and skips itself without them.
 * Unlike the two one-shot handlers above, this one has no path that returns
 * before the calls, so there is no case that can run ungated.
 *
 * WHICH TEXT ENTRY THE DRAW ASKS FOR IS NOT ASSERTED, for the reason the
 * sections above give -- fdps_draw_text takes its whole effect through pixels at
 * the VGA aperture and keeps no state -- so PUSH 0x17 at 00036f36 stands on the
 * reviewer's reading of the instruction stream.  What the cases do pin about the
 * draw is that it does not stop the deployment or the store that follows it.
 * ch03_stage's text block is the same empty-stream fixture, now one entry longer
 * so that 0x17 resolves inside it, so a draw paints nothing and cannot stand a
 * modal wait on a keyboard nothing is typing at.
 */

/* The wave the handler asks for: PUSH 0xf at 00036f49.  It is one past the wave
   the tile-triggered reinforcement above asks for, which is why the record
   tagged 0xe is left in the table underneath it. */
#define CH03_TURN_LIMIT_WAVE 0xf

/* The third record added on top of ch03_stage's five, tagged one wave past the
   one asked for, and the character id that tells it apart from the other two.
   Its tile is not read back anywhere: it is only ever the record a wrong wave
   number would have deployed, and the character id alone says that it was. */
#define CH03_WAVE16_RECORD 7
#define CH03_WAVE16_CHAR_ID 13
#define CH03_TURN_LIMIT_SPAWN_COUNT 8

/* Five turn counters spanning everything the sibling turn-scheduled handler's
   DEC EAX cares about -- the two it compares against, one either side, and one
   past the last turn map02.dat schedules.  This handler has no CMP against
   data_fdps_battle_turn_counter anywhere in its 0x54 bytes, so all five have to
   deploy the same wave. */
#define CH03_TURN_LIMIT_TURN_COUNT 5

/* ch03_stage's map and its one unit at (0, 0), with the wave-14, wave-15 and
   wave-16 records added behind its five, and the battle-end code put where the
   case wants to see it changed from. */
static void ch03_gameover_stage(int battle_turn, int end_code)
{
    ch03_stage(battle_turn);

    ch03_set_spawn(CH03_WAVE14_RECORD, CH03_WAVE14_CHAR_ID,
                   CH03_TURN_LIMIT_WAVE - 1);
    ch03_set_spawn(CH03_WAVE15_RECORD, CH03_WAVE15_CHAR_ID,
                   CH03_TURN_LIMIT_WAVE);
    ch03_set_spawn(CH03_WAVE16_RECORD, CH03_WAVE16_CHAR_ID,
                   CH03_TURN_LIMIT_WAVE + 1);
    ch03_spawn_table[CH03_SPAWN_TABLE_COUNT_OFFSET] =
        CH03_TURN_LIMIT_SPAWN_COUNT;

    data_fdps_chapter_event_or_battle_end_code = (unsigned int) end_code;
}

/* The wave brought on is the literal 15: the wave-15 record, table index 6,
   lands on MAP02.COD record 6 at (22, 15) carrying CH03_WAVE15_CHAR_ID.  The
   wave-14 record underneath it would land on (25, 13) with CH03_WAVE14_CHAR_ID
   and the wave-16 record above it would carry CH03_WAVE16_CHAR_ID, so either
   neighbour is visible twice over -- on the tile and on the id -- and exactly
   one unit arriving is what rules out a handler that deployed more than the
   wave it named. */
static void ch03_gameover_deploys_wave_fifteen(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    fdps_chapter_03_event_turn_limit_game_over(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
    CHECK_EQ((int) ch03_unit(1)->pos_y, CH03_WAVE15_TILE_Y);
}

/* The defeat code reaches the global on the same call that deploys, which is
   what pins the store at 00036f55 as being past both CALLs rather than in front
   of them or on a path either of them could leave: the count and the code are
   read back together after one call. */
static void ch03_gameover_sets_the_defeat_code(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    fdps_chapter_03_event_turn_limit_game_over(0);

    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
    CHECK_EQ(data_fdps_map_unit_count, 2);
}

/* MOV dword ptr [0x00069da0],0x1 is an unconditional store of a literal, not a
   compare-and-set and not an or, and nothing in the 0x54 bytes reads the global
   first: a chapter already marked cleared is turned into a defeat by it, and the
   deployment happens all the same. */
static void ch03_gameover_overwrites_a_cleared_chapter(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_CLEARED);
    fdps_chapter_03_event_turn_limit_game_over(0);

    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
}

/* There is no one-shot latch: the body carries no CMP against
   data_fdps_map_cell_event_triggered_flags at all, so the two slots the rest of
   the family guards itself with -- 0x10, shared by the ambush and the chapter 5
   handler, and 0x11, the tile-triggered reinforcement's own -- can both be up
   and the handler still runs.  Neither is written either, which is what says the
   handler has no latch rather than a latch on a third slot. */
static void ch03_gameover_has_no_one_shot_latch(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT] = 1;
    data_fdps_map_cell_event_triggered_flags[CH03_REINFORCE_LATCH_SLOT] = 1;

    fdps_chapter_03_event_turn_limit_game_over(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH05_LATCH_SLOT], 1);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[
                 CH03_REINFORCE_LATCH_SLOT], 1);
}

/* Nothing guards the body and nothing records that it ran, the battle-end code
   the handler itself just wrote included, so a second call deploys the wave a
   second time.  What makes the giants arrive once in the game is map02.dat's
   turn table naming this slot in one record and no other file naming slot 5.
   The second call is made with the array exactly as the first left it, so the
   second arrival shows as a third unit; its tile is not asserted because the
   first giant is standing on (22, 15) by then and the nearest-free-tile search
   has to put the second one somewhere else. */
static void ch03_gameover_runs_again_every_time_it_is_called(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    fdps_chapter_03_event_turn_limit_game_over(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);

    fdps_chapter_03_event_turn_limit_game_over(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch03_unit(2)->char_id, CH03_WAVE15_CHAR_ID);
    CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
}

/* The map the wave is placed under is the literal 2 pushed at 00036f4b and not
   data_fdps_chapter_current_chapter_id, which the chapter 10, 17 and 18 handlers
   push at the same argument.  The wave-15 record lands on MAP02.COD's record 6
   at (22, 15) with that global on 0 and again with it on 1 -- MAP00.COD's
   record 6 and MAP01.COD's are tiles of their own, so a handler that read the
   global would land somewhere else in at least one of the two. */
static void ch03_gameover_map_number_is_the_literal_two(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    data_fdps_chapter_current_chapter_id = 0;
    fdps_chapter_03_event_turn_limit_game_over(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
    CHECK_EQ((int) ch03_unit(1)->pos_y, CH03_WAVE15_TILE_Y);

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    data_fdps_chapter_current_chapter_id = 1;
    fdps_chapter_03_event_turn_limit_game_over(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
    CHECK_EQ((int) ch03_unit(1)->pos_y, CH03_WAVE15_TILE_Y);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00036f46 -- so the giants
   are put on the nearest free walkable tile to their placement record rather
   than on the record's own tile.  Taking (22, 15) out of the search by giving
   that one cell a tile id whose attribute row is terrain 5 moves the unit one
   tile down: the scan is row-major over the whole grid and a tie is accepted, so
   the last candidate at the best distance wins.  A flag of 1 would drop it on
   (22, 15) regardless of the terrain there. */
static void ch03_gameover_places_on_the_nearest_free_tile(void)
{
    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
    ch03_set_tile_id(CH03_WAVE15_TILE_X, CH03_WAVE15_TILE_Y, 1);
    ch03_set_terrain(1, CH03_TERRAIN_BLOCKED);

    fdps_chapter_03_event_turn_limit_game_over(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
    CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
    CHECK_EQ((int) ch03_unit(1)->pos_y, CH03_WAVE15_TILE_Y + 1);
}

/* The turn counter is never read: there is no CMP against
   data_fdps_battle_turn_counter and no DEC anywhere in the body, so the wave is
   the literal 15 on every turn.  The five counters below are the ones the
   turn-scheduled sibling's arithmetic separates -- a handler that had copied it
   would ask for turn minus one and land on a different record, or on none, at
   four of the five. */
static void ch03_gameover_ignores_the_turn_counter(void)
{
    static int turns[CH03_TURN_LIMIT_TURN_COUNT] = {
        CH03_ZERO_TURN, CH03_EARLIEST_TURN, CH03_FIRST_WAVE_TURN,
        CH03_LAST_WAVE_TURN, CH03_UNSCHEDULED_TURN
    };
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < CH03_TURN_LIMIT_TURN_COUNT; i++) {
        ch03_gameover_stage(turns[i], END_CODE_RUNNING);
        fdps_chapter_03_event_turn_limit_game_over(0);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
        CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
    }
}

/* The incoming argument slot is overwritten with 0 at 00036f1c, before anything
   else in the body, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for, the placement flag or the
   code stored.  The turn-event runner is the only path this slot is reached by
   in the shipped data and it pushes a literal 0; the values put through below
   are that 0, two real unit indices of the kind the tile, cell-search and
   death-script dispatchers of the same table forward, and two that are not
   indices at all. */
static void ch03_gameover_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 7, -1, 30000};
    int i;

    ch03_ensure_game_files();
    if (!ch03_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch03_gameover_stage(CH03_QUIET_TURN, END_CODE_RUNNING);
        fdps_chapter_03_event_turn_limit_game_over(arguments[i]);

        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch03_unit(1)->char_id, CH03_WAVE15_CHAR_ID);
        CHECK_EQ((int) ch03_unit(1)->pos_x, CH03_WAVE15_TILE_X);
        CHECK_EQ(data_fdps_chapter_event_or_battle_end_code, END_CODE_DEFEAT);
    }
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
    RUN_TEST(ch05_advance_clears_exactly_the_range);
    RUN_TEST(ch05_advance_keeps_the_high_nibble);
    RUN_TEST(ch05_advance_raises_the_latch);
    RUN_TEST(ch05_advance_runs_once_per_chapter);
    RUN_TEST(ch05_advance_latch_tests_against_zero);
    RUN_TEST(ch05_advance_touches_no_neighbouring_byte);
    RUN_TEST(ch05_advance_ignores_the_unit_index_argument);
    RUN_TEST(ch07_advance_clears_exactly_the_range);
    RUN_TEST(ch07_advance_keeps_the_high_nibble);
    RUN_TEST(ch07_advance_has_no_one_shot_latch);
    RUN_TEST(ch07_advance_touches_no_neighbouring_byte);
    RUN_TEST(ch07_advance_ignores_the_unit_index_argument);
    RUN_TEST(ch03_record_shape_matches_the_offsets);
    RUN_TEST(ch03_wave_is_the_turn_counter_less_one);
    RUN_TEST(ch03_the_speaking_turns_still_deploy);
    RUN_TEST(ch03_map_number_is_the_literal_two);
    RUN_TEST(ch03_turn_zero_deploys_nothing);
    RUN_TEST(ch03_unscheduled_turn_asks_for_its_own_wave);
    RUN_TEST(ch03_places_on_the_nearest_free_tile);
    RUN_TEST(ch03_has_no_one_shot_latch);
    RUN_TEST(ch03_ignores_the_unit_index_argument);
    RUN_TEST(ch03_ambush_deploys_wave_one);
    RUN_TEST(ch03_ambush_raises_only_its_own_latch_slot);
    RUN_TEST(ch03_ambush_runs_once_per_chapter);
    RUN_TEST(ch03_ambush_latch_tests_against_zero);
    RUN_TEST(ch03_ambush_blocked_call_does_nothing);
    RUN_TEST(ch03_ambush_ignores_the_turn_counter);
    RUN_TEST(ch03_ambush_map_number_is_the_literal_two);
    RUN_TEST(ch03_ambush_places_on_the_nearest_free_tile);
    RUN_TEST(ch03_ambush_ignores_the_unit_index_argument);
    RUN_TEST(ch03_reinforce_record_shape_matches_the_offsets);
    RUN_TEST(ch03_reinforce_deploys_wave_fourteen);
    RUN_TEST(ch03_reinforce_ignores_a_unit_on_side_zero);
    RUN_TEST(ch03_reinforce_fires_on_every_non_zero_side);
    RUN_TEST(ch03_reinforce_reads_the_side_of_the_named_unit);
    RUN_TEST(ch03_reinforce_is_blocked_by_a_raised_latch);
    RUN_TEST(ch03_reinforce_raises_only_its_own_latch_slot);
    RUN_TEST(ch03_reinforce_runs_once_per_chapter);
    RUN_TEST(ch03_reinforce_map_number_is_the_literal_two);
    RUN_TEST(ch03_reinforce_places_on_the_nearest_free_tile);
    RUN_TEST(ch03_gameover_deploys_wave_fifteen);
    RUN_TEST(ch03_gameover_sets_the_defeat_code);
    RUN_TEST(ch03_gameover_overwrites_a_cleared_chapter);
    RUN_TEST(ch03_gameover_has_no_one_shot_latch);
    RUN_TEST(ch03_gameover_runs_again_every_time_it_is_called);
    RUN_TEST(ch03_gameover_map_number_is_the_literal_two);
    RUN_TEST(ch03_gameover_places_on_the_nearest_free_tile);
    RUN_TEST(ch03_gameover_ignores_the_turn_counter);
    RUN_TEST(ch03_gameover_ignores_the_unit_index_argument);
}
