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
 */
#include <stddef.h>
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
}
