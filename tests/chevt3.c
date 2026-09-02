/* tests/chevt3.c -- cover for src/chevt3.c.
 *
 * The chapter 15 handler at 00037af0 is the chapter 13 handler's shape with the
 * range 0x1d..0x25 in place of 9..0x2c, and is covered the same way: its two
 * bounds, the signed inclusive compare between them and the 0xf0 mask are all
 * literals in its instruction stream -- MOV dword ptr [EBP-0x20],0x1d at
 * 00037b03, MOV dword ptr [EBP-0x1c],0x25 at 00037b0a, CMP EAX,[EBP-0x10] / JLE
 * at 00037b33, and AND DL,0xf0 at 00037b53 -- and the absence of a guard in
 * front of its loop is asserted by putting the shared one-shot latch slot up and
 * watching it run anyway.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores.
 * What the global itself holds is ticket 23's and is not asserted, so every
 * case writes the state it wants to see changed.
 *
 * The chapter 16 handler at 00038020 is the same expansion twice over behind an
 * equality on data_fdps_battle_turn_counter, so it is covered the same way with
 * one thing added: which branch a turn number reaches.  That test is what pins
 * the fall-through down as a fall-through -- the map schedules turns 5 and 15
 * and only 5 is compared for -- so several other turns are put through it and
 * all of them have to land on the lower range.  The counter is set by the test
 * because it is a global ticket 23 has not written yet; what it holds outside a
 * battle is not asserted.
 *
 * Which indices the range means comes from map14.dat: 9 player records are laid
 * down first at 0..8 and the file's wave-0 deployment records 1..43 follow in
 * file order, so unit index = deployment record index + 8 and 0x1d..0x25 are
 * records 21..29 -- the eight-unit bridgehead block at the top right of the map
 * plus record 27, the beam turret.  Both ends of the range are pinned hardest,
 * because the inclusive top bound is what every obvious rewrite of the loop
 * gets wrong at exactly one record.
 *
 * The chapter 17 cases in the last third of the file stage differently and say
 * why in their own note: that handler's whole body is a call into
 * fdps_deploy_wave, which opens ICON.CEL and FIELD.VFS for itself, so the
 * cases need those files and skip themselves without them.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt3.h"

/* The single inclusive range the one inline loop covers, read off the
   constants at 00037b03 (0x1d) and 00037b0a (0x25) with the signed JLE at
   00037b36. */
#define CH15_FIRST_INDEX 0x1d
#define CH15_LAST_INDEX  0x25

/* How many records that range covers: 0x25 - 0x1d + 1, the nine units the
   handler is named for. */
#define CH15_RANGE_UNITS 9

/* The last unit index chapter 15's map deploys in wave 0: map14.dat's records
   1..43 land at 9..0x33, so the range stops well short of the end of the array
   and everything above 0x25 has to keep the behaviour the map gave it. */
#define CH15_LAST_DEPLOYED_INDEX 0x33

/* Enough records to hold that whole deployment and a few past it, so an
   off-by-one at either end of the range has somewhere visible to land. */
#define CH15_STAGE_UNITS 0x38

/* The unit whose death script names this handler: map14.dat's deployment
   record 8, the level 19 ice mage inside the stockade, which becomes unit
   index 8 + 8. */
#define CH15_DEATH_SCRIPT_UNIT_INDEX 0x10

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers
   of this family latch -- element 0x10, the first the map's own event codes
   cannot reach.  This handler does not use it, and that is what is asserted. */
#define CH15_LATCH_SLOT 0x10

static struct fdps_unit_record ch15_units[CH15_STAGE_UNITS];

/* Give every record the same AI byte and point the array global at the block.
   The staged value carries a high nibble as well as a behaviour code, because
   the whole point of the merge is that only one of the two moves. */
static void stage_ch15_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch15_units;
    for (i = 0; i < (int) sizeof(ch15_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        ch15_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch15_units;
}

/* The record has to be 0x50 bytes with its AI byte at +0x34 for the emitted C
   to address the byte the MOV byte ptr [EAX+0x34] store at 00037b5e addresses;
   the stride is the IMUL 0x50 inside fdps_get_unit_record. */
static void ch15_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Exactly indices 0x1d..0x25 are rewritten and every record either side of the
   range is left as it was.  The staged 0x52 is behaviour code 2 -- hold
   position, which is what byte 0x11 of map14.dat's deployment records gives its
   units -- under a high nibble of 0x50; the range comes out 0x50 because the
   mode ORed in is 0, and the rest keep 0x52.  Indices 0..0x1c cover the nine
   player records and the first twelve deployed enemies, which the range starts
   above, and 0x26..0x37 run past the last deployed unit at 0x33: both ends have
   to be untouched. */
static void ch15_activate_clears_exactly_the_range(void)
{
    int i;

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);

    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        if (i >= CH15_FIRST_INDEX && i <= CH15_LAST_INDEX) {
            CHECK_EQ(ch15_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch15_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers nine records, not eight: 0x25 - 0x1d + 1, and the top bound
   is inclusive because the compare at 00037b33 is JLE.  The count is asserted
   by counting the records that moved, and both boundary records are named on
   their own -- 0x25 must move and 0x26 must not -- because a rewrite of the
   loop as i < 0x25 is exactly the mistake that leaves the last of the nine
   units holding position and changes nothing else. */
static void ch15_activate_includes_the_last_index(void)
{
    int i;
    int moved;

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);

    moved = 0;
    for (i = 0; i < CH15_STAGE_UNITS; i++) {
        if (ch15_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH15_RANGE_UNITS);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 00037b53 and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI
   flags read elsewhere -- fdps_map_actor_take_best_action at 00012c72 and
   fdps_score_targets_for_item at 00013380 -- so a merge that assigned the mode
   whole, or that masked with anything wider, would drop them.  Expected values
   are the staged byte ANDed with 0xf0. */
static void ch15_activate_keeps_the_high_nibble(void)
{
    stage_ch15_units(0);
    ch15_units[0x1d].ai_behavior = 0xc2;
    ch15_units[0x1e].ai_behavior = 0x02;
    ch15_units[0x1f].ai_behavior = 0xff;
    ch15_units[0x21].ai_behavior = 0x40;
    ch15_units[0x25].ai_behavior = 0x8b;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(ch15_units[0x1d].ai_behavior, 0xc0);
    CHECK_EQ(ch15_units[0x1e].ai_behavior, 0x00);
    CHECK_EQ(ch15_units[0x1f].ai_behavior, 0xf0);
    CHECK_EQ(ch15_units[0x21].ai_behavior, 0x40);
    CHECK_EQ(ch15_units[0x25].ai_behavior, 0x80);
}

/* Nothing guards the loop: the instruction after the argument-slot store at
   00037afc is the first of the three constant stores, with no compare between
   them, so this handler has no one-shot latch and runs its loop every time it
   is called.  The latch slot is put up before the call and the range still
   moves; the slot is also asserted unchanged, because a handler that had grown
   a latch would have written it. */
static void ch15_activate_has_no_one_shot_latch(void)
{
    stage_ch15_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   -- the death-script operand's high byte and ai_dest_x -- is visible; 0x55 is
   also a value whose low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the range are checked, because the
   stride the store is indexed by is the IMUL 0x50 inside fdps_get_unit_record
   and an error in it shows up furthest from the base. */
static void ch15_activate_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch15_units;
    for (i = 0; i < (int) sizeof(ch15_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_15_event_activate_enemy_group(0);

    CHECK_EQ(bytes[0x1d * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x1d * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x1d * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x25 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x1c * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x26 * 0x50 + 0x34], 0x55);
}

/* The incoming argument slot is overwritten with 0 at 00037afc and never read,
   and no loop bound comes from it, so the index the dispatcher passes cannot
   reach the result.  The death-script runner is the only path this slot is
   reached by in the shipped data and it pushes its own actor index at 0001dcad
   -- the unit that landed the killing blow, so one of the nine player records
   at 0..8 -- while the unit whose script it is sits at 0x10; both are passed
   here, along with an index inside the range, and 0x26, -1 and 30000, which are
   the ones an argument-driven handler would betray itself on. */
static void ch15_activate_ignores_the_unit_index_argument(void)
{
    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_DEATH_SCRIPT_UNIT_INDEX);
    CHECK_EQ(ch15_units[CH15_DEATH_SCRIPT_UNIT_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(0);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_FIRST_INDEX);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(CH15_LAST_INDEX + 1);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX + 1].ai_behavior, 0x52);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(-1);
    CHECK_EQ(ch15_units[CH15_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);

    stage_ch15_units(0x52);
    fdps_chapter_15_event_activate_enemy_group(30000);
    CHECK_EQ(ch15_units[CH15_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch15_units[0].ai_behavior, 0x52);
}

/* The two inclusive ranges the chapter 16 handler's two inline loops cover,
   read off the constants at 0003803c (0x19) and 00038043 (0x22) for the turn-5
   branch and 0003809e (0x0a) and 000380a5 (0x19) for the fall-through, each
   with a signed JLE. */
#define CH16_TURN5_FIRST_INDEX 0x19
#define CH16_TURN5_LAST_INDEX  0x22
#define CH16_OTHER_FIRST_INDEX 0x0a
#define CH16_OTHER_LAST_INDEX  0x19

/* How many records each range covers: 0x22 - 0x19 + 1 and 0x19 - 0x0a + 1. */
#define CH16_TURN5_UNITS 10
#define CH16_OTHER_UNITS 16

/* The one turn number the equality at 00038033 singles out.  Every other value
   falls through to the second loop, which is why the second is exercised with
   several turns and not just the 15 the map schedules. */
#define CH16_RELEASE_TURN 5

/* The other turn map15.dat's turn-event table names for this slot.  It reaches
   the same fall-through branch as any other non-5 turn; it is used here
   because it is what the shipped data actually fires. */
#define CH16_SHIPPED_SECOND_TURN 15

/* Enough records to hold chapter 16's whole deployment -- 10 player records at
   0..9 and 25 enemy records at 0x0a..0x22 -- and five past the top, so an
   off-by-one at either end of either range has somewhere visible to land. */
#define CH16_STAGE_UNITS 0x28

static struct fdps_unit_record ch16_units[CH16_STAGE_UNITS];

/* Stage the array the same way the chapter 15 cases do, and set the turn the
   handler is to read.  The staged AI byte carries a high nibble as well as a
   behaviour code, because the whole point of the merge is that only one of the
   two moves. */
static void stage_ch16_units(int battle_turn, int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch16_units;
    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        ch16_units[i].ai_behavior = (unsigned char) ai_behavior;
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch16_units;
    data_fdps_battle_turn_counter = battle_turn;
}

/* On turn 5 exactly indices 0x19..0x22 are rewritten and everything either
   side of them keeps what the map gave it.  The staged 0x52 is behaviour code
   2 -- hold position, which is what byte 0x11 of map15.dat's deployment
   records gives these units -- under a high nibble of 0x50; the range comes out
   0x50 because the mode ORed in is 0.  Indices 0..0x18 are the ten player
   records and waves 0 and 1, which this branch must not touch, and 0x23..0x27
   run past the last deployed unit at 0x22. */
static void ch16_turn5_clears_the_second_wave_block(void)
{
    int i;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (i >= CH16_TURN5_FIRST_INDEX && i <= CH16_TURN5_LAST_INDEX) {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x52);
        }
    }
}

/* On the turn the map schedules second, 15, the fall-through loop runs and
   exactly indices 0x0a..0x19 are rewritten: waves 0 and 1 plus the first
   wave-2 flyer.  The ten player records at 0..9 sit below the range and
   0x1a..0x27 above it, and both have to be untouched. */
static void ch16_other_turn_clears_the_opening_block(void)
{
    int i;

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (i >= CH16_OTHER_FIRST_INDEX && i <= CH16_OTHER_LAST_INDEX) {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch16_units[i].ai_behavior, 0x52);
        }
    }
}

/* Both top bounds are inclusive, because both compares are JLE -- 0003806c for
   the turn-5 loop and 000380ce for the other -- so each range is one record
   longer than a rewrite with < would make it.  The counts are asserted by
   counting the records that moved and both boundary records of each range are
   named on their own: for turn 5 that is 0x22 moving and 0x23 not, and for the
   fall-through 0x19 moving and 0x1a not.  Index 0x19 is checked in both, since
   it is the top of one range and the bottom of the other. */
static void ch16_both_ranges_include_their_last_index(void)
{
    int i;
    int moved;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    moved = 0;
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (ch16_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH16_TURN5_UNITS);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX + 1].ai_behavior, 0x52);

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);

    moved = 0;
    for (i = 0; i < CH16_STAGE_UNITS; i++) {
        if (ch16_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH16_OTHER_UNITS);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The branch is an equality on the turn counter, not a range test and not a
   test for turn 15: CMP dword ptr [0x00069ce8],0x5 / JNZ 0003809e at 00038033.
   So 5 is the only value that reaches the upper range and every other value --
   below it, just above it, the 15 the map schedules, and values no battle can
   reach -- lands on the lower one.  Each turn is checked at both ranges' first
   index, which is the pair that separates the branches. */
static void ch16_only_turn_five_takes_the_upper_range(void)
{
    static int other_turns[6] = {0, 1, 4, 6, 15, 30000};
    int i;

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x52);

    for (i = 0; i < 6; i++) {
        stage_ch16_units(other_turns[i], 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(0);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch16_units(-1, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 0003808c and
   000380ee, and the low nibble ends at 0 whatever it held.  0x40 and 0x80 are
   the two AI flags read elsewhere, so a merge that assigned the mode whole, or
   masked with anything wider, would drop them.  Expected values are the staged
   byte ANDed with 0xf0, and both loops are checked because they are two
   separate copies of the merge in the instruction stream. */
static void ch16_keeps_the_high_nibble(void)
{
    stage_ch16_units(CH16_RELEASE_TURN, 0);
    ch16_units[0x19].ai_behavior = 0xc2;
    ch16_units[0x1a].ai_behavior = 0x02;
    ch16_units[0x1f].ai_behavior = 0xff;
    ch16_units[0x21].ai_behavior = 0x40;
    ch16_units[0x22].ai_behavior = 0x8b;
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[0x19].ai_behavior, 0xc0);
    CHECK_EQ(ch16_units[0x1a].ai_behavior, 0x00);
    CHECK_EQ(ch16_units[0x1f].ai_behavior, 0xf0);
    CHECK_EQ(ch16_units[0x21].ai_behavior, 0x40);
    CHECK_EQ(ch16_units[0x22].ai_behavior, 0x80);

    stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0);
    ch16_units[0x0a].ai_behavior = 0xc2;
    ch16_units[0x11].ai_behavior = 0xff;
    ch16_units[0x18].ai_behavior = 0x40;
    ch16_units[0x19].ai_behavior = 0x8b;
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[0x0a].ai_behavior, 0xc0);
    CHECK_EQ(ch16_units[0x11].ai_behavior, 0xf0);
    CHECK_EQ(ch16_units[0x18].ai_behavior, 0x40);
    CHECK_EQ(ch16_units[0x19].ai_behavior, 0x80);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first, so a store that landed at +0x33 or +0x35
   is visible, and 0x55's low nibble is not already 0, so the write that should
   happen is visible too.  Both ends of the running range are checked, because
   the stride the store is indexed by is the IMUL 0x50 inside
   fdps_get_unit_record and an error in it shows up furthest from the base. */
static void ch16_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch16_units;
    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_map_unit_array_ptr = bytes;
    data_fdps_battle_turn_counter = CH16_RELEASE_TURN;

    fdps_chapter_16_event_enemies_advance_for_turn(0);

    CHECK_EQ(bytes[0x19 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x22 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x18 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x23 * 0x50 + 0x34], 0x55);

    for (i = 0; i < (int) sizeof(ch16_units); i++) {
        bytes[i] = 0x55;
    }
    data_fdps_battle_turn_counter = CH16_SHIPPED_SECOND_TURN;

    fdps_chapter_16_event_enemies_advance_for_turn(0);

    CHECK_EQ(bytes[0x0a * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x0a * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x0a * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x19 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x09 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x1a * 0x50 + 0x34], 0x55);
}

/* Nothing guards either loop -- the only compare in the body is the one on the
   turn counter -- so the handler has no one-shot latch and runs its loop every
   time it is called.  The latch slot the one-shot handlers of this family use
   is put up before the call and the range still moves, and the slot is
   asserted unchanged because a handler that had grown a latch would have
   written it.  A second call on the same turn is made too: the merge is
   idempotent, so it must leave the same values. */
static void ch16_has_no_one_shot_latch(void)
{
    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    stage_ch16_units(CH16_RELEASE_TURN, 0x52);
    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);

    fdps_chapter_16_event_enemies_advance_for_turn(0);
    CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The incoming argument slot is overwritten with 0 at 0003802c before the turn
   counter is even read, and never read back, so the index the dispatcher
   passes cannot reach the result and cannot pick a branch either.  The
   turn-event runner is the only path this slot is reached by in the shipped
   data and it pushes a literal 0 at 0002e13e; the values passed here are that
   0, an index inside each range, one just past the upper range, and -1 and
   30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch16_ignores_the_unit_index_argument(void)
{
    static int arguments[6] = {0, 0x0a, 0x19, 0x23, -1, 30000};
    int i;

    for (i = 0; i < 6; i++) {
        stage_ch16_units(CH16_RELEASE_TURN, 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(arguments[i]);
        CHECK_EQ(ch16_units[CH16_TURN5_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x52);

        stage_ch16_units(CH16_SHIPPED_SECOND_TURN, 0x52);
        fdps_chapter_16_event_enemies_advance_for_turn(arguments[i]);
        CHECK_EQ(ch16_units[CH16_OTHER_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_OTHER_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch16_units[CH16_TURN5_LAST_INDEX].ai_behavior, 0x52);
    }
}

/* Chapter 17's turn-scheduled reinforcement at 00038110, from here down.
 *
 * The handler's whole body is one call, and none of the three values it hands
 * fdps_deploy_wave -- PUSH dword ptr [0x00069cf4], the counter less seven, and
 * a zeroed EAX, at 00038123..0003812f -- is left anywhere afterwards.  So the
 * only way to see any of them is to let the deployment happen, and these cases
 * run fdps_deploy_wave for real, then read back what landed where:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's own wave cases and the chapter 10 cases
 * in tests/chevt2.c expect, staged through tests/gamefile.lst.  That function
 * opens ICON.CEL and FIELD.VFS for itself and a run without them would not
 * fail a check, it would hang in fdps_wait_any_key, so every case here skips
 * itself when they are not there.
 *
 * Which wave the spawn table's records carry is staged rather than read from
 * map16.dat, because what is under test is the arithmetic that picks a wave
 * and not what chapter 17 happens to have tagged: a record is planted at each
 * of the waves either side of the one the turn should ask for, so asking for
 * the wrong one is visible as a different character id on different tiles.
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* What the handler subtracts from the turn counter: SUB EAX,0x7 at 0003812b.
   Turns are written here as turn numbers and the wave each should reach is
   derived from them, so a test that agreed with a wrong offset would have to
   disagree with the turn the map schedules. */
#define CH17_WAVE_TURN_OFFSET 7

/* The two turns map16.dat's turn-event table names for this slot, {turn 8,
   slot 24, phase 0} and {turn 9, slot 24, phase 0}, which are the turns that
   reach waves 1 and 2. */
#define CH17_FIRST_SCHEDULED_TURN 8
#define CH17_SECOND_SCHEDULED_TURN 9

/* The turn whose key is wave 0, the group the map opens with.  Nothing
   schedules it; it is here because it is the value a clamped subtraction would
   turn every earlier turn into. */
#define CH17_WAVE_ZERO_TURN 7

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search fdps_deploy_unit runs on a place_exact of 0 walks the whole grid, and
   the marking pass writes at a unit's own tile with no bound, so the grid has
   to cover the coordinates the real placement file names. */
#define CH17_GRID_W 32
#define CH17_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH17_SPAWN_TABLE_RECORD_BASE 0x83
#define CH17_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile
   map's width word at +7 with its 16-bit ids from +0xb, the attribute table's
   4-byte rows from +0x11, and the event layer's width at +7 with its cells
   from +0x10 (src/maptile.c). */
#define CH17_TILE_MAP_WIDTH_OFFSET 7
#define CH17_TILE_MAP_IDS_OFFSET 0xb
#define CH17_TILE_ATTR_ROWS_OFFSET 0x11
#define CH17_EVENT_LAYER_WIDTH_OFFSET 7
#define CH17_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH17_TERRAIN_WALKABLE 1
#define CH17_TERRAIN_BLOCKED 5

#define CH17_TILE_ATTR_ROWS 16
#define CH17_CHAR_TABLE_ROWS 8
#define CH17_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH17_ITEM_TABLE_ROWS 256
#define CH17_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH17_UNIT_STRIDE 0x50

/* The three staged deployment records and the wave each is tagged with, one at
   each of the waves the two scheduled turns reach and one below them.  The
   character ids are arbitrary and only have to differ, so that which record
   was deployed is readable off the unit. */
#define CH17_WAVE0_RECORD 0
#define CH17_WAVE1_RECORD 1
#define CH17_WAVE2_RECORD 2
#define CH17_WAVE0_CHAR_ID 5
#define CH17_WAVE1_CHAR_ID 6
#define CH17_WAVE2_CHAR_ID 7

static unsigned char ch17_grid[4 + CH17_GRID_W * CH17_GRID_H * 2];
static unsigned char ch17_spawn_table[CH17_SPAWN_TABLE_RECORD_BASE + 8 * 0x1a];
static unsigned char ch17_tile_map[CH17_TILE_MAP_IDS_OFFSET +
                                   CH17_GRID_W * CH17_GRID_H * 2];
static unsigned char ch17_tile_attr[CH17_TILE_ATTR_ROWS_OFFSET +
                                    CH17_TILE_ATTR_ROWS * 4];
static unsigned char ch17_event_layer[CH17_EVENT_LAYER_CELLS_OFFSET +
                                      CH17_GRID_W * CH17_GRID_H];
static struct fdps_character_base_record ch17_char_base[CH17_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch17_growth[CH17_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch17_enemy[CH17_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch17_items[CH17_ITEM_TABLE_ROWS];

static int ch17_files_checked = 0;
static int ch17_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave
   into fdps_wait_any_key and a missing member into exit(1). */
static void ch17_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch17_files_checked) {
        return;
    }
    ch17_files_checked = 1;

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
    ch17_files_ready = 1;
}

static void ch17_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch17_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch17_spawn_table + CH17_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch17_set_spawn(int index, int char_id, int wave_no)
{
    ch17_spawn_at(index)->char_id = (unsigned char) char_id;
    ch17_spawn_at(index)->level = 1;
    ch17_spawn_at(index)->side = 2;
    ch17_spawn_at(index)->equipped_item_0 = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->equipped_item_1 = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[0] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[1] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[2] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[3] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[4] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->carried_items[5] = CH17_ITEM_ID_NONE;
    ch17_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch17_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch17_tile_attr + CH17_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says the deployment search must not use it. */
static void ch17_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch17_tile_map + CH17_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH17_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch17_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH17_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map
   0 and the turn counter on the given turn, plus three deployment records
   tagged waves 0, 1 and 2 at table indices 0, 1 and 2.  Placement record and
   table index are the same number, so which record was deployed is readable
   twice over: off the character id and off the tile it landed on.

   The unit already on the map stands at (0, 0), clear of every placement
   record these cases read back. */
static void ch17_stage(int battle_turn)
{
    int i;

    ch17_zero_bytes(ch17_grid, (int) sizeof(ch17_grid));
    ch17_zero_bytes(ch17_spawn_table, (int) sizeof(ch17_spawn_table));
    ch17_zero_bytes(ch17_tile_map, (int) sizeof(ch17_tile_map));
    ch17_zero_bytes(ch17_tile_attr, (int) sizeof(ch17_tile_attr));
    ch17_zero_bytes(ch17_event_layer, (int) sizeof(ch17_event_layer));
    ch17_zero_bytes(ch17_char_base, (int) sizeof(ch17_char_base));
    ch17_zero_bytes(ch17_growth, (int) sizeof(ch17_growth));
    ch17_zero_bytes(ch17_enemy, (int) sizeof(ch17_enemy));
    ch17_zero_bytes(ch17_items, (int) sizeof(ch17_items));

    *(short *) ch17_grid = (short) CH17_GRID_W;
    *(short *) (ch17_grid + 2) = (short) CH17_GRID_H;

    *(short *) (ch17_tile_map + CH17_TILE_MAP_WIDTH_OFFSET) =
        (short) CH17_GRID_W;
    for (i = 0; i < CH17_TILE_ATTR_ROWS; i++) {
        ch17_set_terrain(i, CH17_TERRAIN_WALKABLE);
    }

    *(short *) (ch17_event_layer + CH17_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH17_GRID_W;

    data_fdps_battle_move_grid_ptr = ch17_grid;
    data_fdps_tile_event_data_table_ptr = ch17_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch17_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch17_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch17_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch17_char_base;
    data_fdps_battle_character_growth_table_ptr = (unsigned char *) ch17_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch17_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch17_items;

    data_fdps_map_unit_array_ptr = (unsigned char *) malloc(CH17_UNIT_STRIDE);
    ch17_zero_bytes(data_fdps_map_unit_array_ptr, CH17_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;

    ch17_spawn_table[CH17_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch17_set_spawn(CH17_WAVE0_RECORD, CH17_WAVE0_CHAR_ID, 0);
    ch17_set_spawn(CH17_WAVE1_RECORD, CH17_WAVE1_CHAR_ID, 1);
    ch17_set_spawn(CH17_WAVE2_RECORD, CH17_WAVE2_CHAR_ID, 2);

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = battle_turn;
}

/* The fields the cases below read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if
   the layout were wrong. */
static void ch17_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH17_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 8);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave asked for is the turn counter less seven and the turns that reach
   waves 1 and 2 are the 8 and 9 map16.dat schedules.  Three records are laid
   down at waves 0, 1 and 2 and exactly one of them deploys each time: turn 8
   brings on the wave-1 record, which is table index 1 and lands on MAP00.COD
   record 1 at (22, 12), and turn 9 the wave-2 record, table index 2 at
   (8, 10).  An offset of 8 or 6 would deploy one of the neighbours instead,
   and both the character id and the tile would say so. */
static void ch17_deploys_the_wave_the_turn_is_due(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 12);

    ch17_stage(CH17_SECOND_SCHEDULED_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 8);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 10);
}

/* The subtraction really is a subtraction and not a lookup keyed on the two
   turns the map happens to schedule: turn 7 is not in map16.dat's turn table
   at all, and it asks for wave 0 -- the wave-0 record, table index 0, on
   MAP00.COD record 0 at (18, 0).  A turn above them is put through as well:
   14 asks for wave 7, which no staged record carries, and the walk matches
   nothing while the file open and load still happen, so the key really is
   unbounded at the top too and not a choice between the map's two turns. */
static void ch17_wave_key_follows_the_counter(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_WAVE_ZERO_TURN);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 0);

    ch17_stage(CH17_WAVE_ZERO_TURN + CH17_WAVE_TURN_OFFSET);
    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The key is not clamped at the bottom, and this is the one that would pass
   just as happily if it were, were it not asserted: every turn below 7 gives a
   negative wave, which matches no record because a record's wave byte is
   unsigned, so nothing at all is deployed.  A clamp to 0 would instead match
   the wave-0 record and put the map's opening army down a second time.  Turns
   1, 6 and the 0 a counter never legitimately holds are all put through it. */
static void ch17_early_turns_deploy_nothing(void)
{
    static int early_turns[3] = {0, 1, 6};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 3; i++) {
        ch17_stage(early_turns[i]);
        fdps_chapter_17_event_deploy_wave_for_turn(0);
        CHECK_EQ(data_fdps_map_unit_count, 1);
        CHECK_EQ((int) ch17_unit(0)->char_id, 0);
    }
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same wave-0 record placed while that global says 1 lands on MAP01.COD's
   record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch17_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_WAVE_ZERO_TURN);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_17_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00038123 -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP00.COD record 1 names
   (22, 12); giving that one cell a tile id whose attribute row is terrain 5
   takes it out of the search, and the unit lands one tile away.  A flag of 1
   would drop it on (22, 12) regardless of the terrain there.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted (CMP EAX,
   [EBP-0x14] / JLE at 000233e6), so the last candidate at the best distance
   wins. */
static void ch17_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    ch17_set_tile_id(22, 12, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    fdps_chapter_17_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 13);
}

/* Nothing guards the call: there is no compare anywhere in the body and no
   latch is written, so a second firing on the same turn deploys the same wave
   again rather than being refused.  The slot the one-shot handlers of this
   family latch is also put up beforehand and the wave still arrives, and the
   slot is asserted unchanged because a handler that had grown a latch would
   have written it. */
static void ch17_has_no_one_shot_latch(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH17_FIRST_SCHEDULED_TURN);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    fdps_chapter_17_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch17_unit(2)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 0003811c before the turn
   counter is read, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for or the placement flag.
   The turn-event runner is the only path this slot is reached by in the
   shipped data and it pushes a literal 0; the values passed here are that 0,
   an index that names the unit already on the map, one past the array, and -1
   and 30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch17_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch17_stage(CH17_FIRST_SCHEDULED_TURN);
        fdps_chapter_17_event_deploy_wave_for_turn(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

/* The chapter 18 handler is the chapter 17 one with the subtraction taken out,
   so the cases below stand on the same fixture: ch17_stage lays down three
   deployment records tagged waves 0, 1 and 2 at table indices 0, 1 and 2 on a
   blank walkable map with one unit already on it, and MAP00.COD's own records
   0, 1 and 2 name (18, 0), (22, 12) and (8, 10).  What changes is which turn
   reaches which record: chapter 17 subtracts seven and chapter 18 subtracts
   nothing, so here the turn IS the wave.  Sharing the fixture is what makes
   that the only difference these cases can be reading. */

/* The turns that name the fixture's three records, being the wave numbers
   themselves.  Chapter 18's own schedule runs 4..11 and 13; those waves are in
   map17.dat rather than in this fixture, and CH18_SCHEDULED_TURN_NO_RECORD is
   one of them, put through to show an unmatched key is carried rather than
   caught. */
#define CH18_WAVE0_TURN 0
#define CH18_WAVE1_TURN 1
#define CH18_WAVE2_TURN 2
#define CH18_UNMATCHED_TURN 3
#define CH18_SCHEDULED_TURN_NO_RECORD 13

/* The wave asked for is the turn counter itself with nothing taken off it.
   Turn 1 brings on the wave-1 record, table index 1, which lands on MAP00.COD
   record 1 at (22, 12), and turn 2 the wave-2 record, index 2 at (8, 10).
   This is the case chapter 17's offset would fail: subtracting seven from
   either turn gives a negative key that matches no record and deploys nothing,
   and any other offset would deploy the neighbouring record, which both the
   character id and the tile would say. */
static void ch18_deploys_the_wave_the_turn_names(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 12);

    ch17_stage(CH18_WAVE2_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 8);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 10);
}

/* The counter reaches the call unadjusted at the bottom of its range too: turn
   0 asks for wave 0, the group a map opens with, and brings the wave-0 record
   on -- MAP00.COD record 0 at (18, 0).  An off-by-one either way would reach
   the wave-1 record or no record at all. */
static void ch18_turn_zero_asks_for_wave_zero(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE0_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 0);
}

/* A key that matches nothing is carried through rather than caught or folded
   onto a wave that does exist: turn 3 and turn 13 both walk the table, match
   no record and deploy nobody.  A clamp or a fallback to wave 0 would put the
   map's opening group down a second time and the count would say so.  Turn 13
   is one of chapter 18's own nine scheduled turns, so the top of its real
   range is unbounded here as well. */
static void ch18_unmatched_turns_deploy_nothing(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_UNMATCHED_TURN);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);

    ch17_stage(CH18_SCHEDULED_TURN_NO_RECORD);
    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 1);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the same wave-0 record placed while that global says 1 lands on MAP01.COD's
   record 0 at (9, 4) instead of MAP00.COD's (18, 0). */
static void ch18_map_number_comes_from_the_chapter_global(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE0_TURN);
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_18_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE0_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 4);
}

/* The placement flag is 0 -- XOR EAX,EAX / PUSH EAX at 00038163 -- so the
   reinforcements are put on the nearest free walkable tile to their placement
   record rather than on the record's own tile.  MAP00.COD record 1 names
   (22, 12); giving that one cell a tile id whose attribute row is terrain 5
   takes it out of the search and the unit lands one tile away.  A flag of 1
   would drop it on (22, 12) regardless of the terrain there.

   (22, 13) is which of the four tiles at distance 1 it lands on, because the
   scan is row-major over the whole grid and a tie is accepted, so the last
   candidate at the best distance wins. */
static void ch18_places_on_the_nearest_free_tile(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    ch17_set_tile_id(22, 12, 1);
    ch17_set_terrain(1, CH17_TERRAIN_BLOCKED);

    fdps_chapter_18_event_deploy_wave_for_turn(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch17_unit(1)->pos_y, 13);
}

/* Nothing guards the call: there is no compare anywhere in the body and no
   latch is written, so a second firing on the same turn deploys the same wave
   again rather than being refused.  The slot the one-shot handlers of this
   family latch is also put up beforehand and the wave still arrives, and the
   slot is asserted unchanged because a handler that had grown a latch would
   have written it. */
static void ch18_has_no_one_shot_latch(void)
{
    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    ch17_stage(CH18_WAVE1_TURN);
    data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT] = 1;

    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);

    fdps_chapter_18_event_deploy_wave_for_turn(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch17_unit(2)->char_id, CH17_WAVE1_CHAR_ID);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH15_LATCH_SLOT], 1);
}

/* The incoming argument slot is overwritten with 0 at 0003815c before either
   global is read, and never read back, so the index the dispatcher passes
   cannot reach the wave asked for, the map asked for or the placement flag.
   The turn-event runner is the only path this slot is reached by in the
   shipped data and it pushes a literal 0; the values passed here are that 0,
   an index that names the unit already on the map, one past the array, and -1
   and 30000, which are the ones an argument-driven handler would betray itself
   on. */
static void ch18_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch17_ensure_game_files();
    if (!ch17_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch17_stage(CH18_WAVE1_TURN);
        fdps_chapter_18_event_deploy_wave_for_turn(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch17_unit(1)->char_id, CH17_WAVE1_CHAR_ID);
        CHECK_EQ((int) ch17_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch17_unit(1)->pos_y, 12);
    }
}

void run_chevt3_tests(void)
{
    RUN_TEST(ch15_record_shape_matches_the_offsets);
    RUN_TEST(ch15_activate_clears_exactly_the_range);
    RUN_TEST(ch15_activate_includes_the_last_index);
    RUN_TEST(ch15_activate_keeps_the_high_nibble);
    RUN_TEST(ch15_activate_has_no_one_shot_latch);
    RUN_TEST(ch15_activate_touches_no_neighbouring_byte);
    RUN_TEST(ch15_activate_ignores_the_unit_index_argument);
    RUN_TEST(ch16_turn5_clears_the_second_wave_block);
    RUN_TEST(ch16_other_turn_clears_the_opening_block);
    RUN_TEST(ch16_both_ranges_include_their_last_index);
    RUN_TEST(ch16_only_turn_five_takes_the_upper_range);
    RUN_TEST(ch16_keeps_the_high_nibble);
    RUN_TEST(ch16_touches_no_neighbouring_byte);
    RUN_TEST(ch16_has_no_one_shot_latch);
    RUN_TEST(ch16_ignores_the_unit_index_argument);
    RUN_TEST(ch17_record_shape_matches_the_offsets);
    RUN_TEST(ch17_deploys_the_wave_the_turn_is_due);
    RUN_TEST(ch17_wave_key_follows_the_counter);
    RUN_TEST(ch17_early_turns_deploy_nothing);
    RUN_TEST(ch17_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch17_places_on_the_nearest_free_tile);
    RUN_TEST(ch17_has_no_one_shot_latch);
    RUN_TEST(ch17_ignores_the_unit_index_argument);
    RUN_TEST(ch18_deploys_the_wave_the_turn_names);
    RUN_TEST(ch18_turn_zero_asks_for_wave_zero);
    RUN_TEST(ch18_unmatched_turns_deploy_nothing);
    RUN_TEST(ch18_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch18_places_on_the_nearest_free_tile);
    RUN_TEST(ch18_has_no_one_shot_latch);
    RUN_TEST(ch18_ignores_the_unit_index_argument);
}
