/* tests/chevt6.c -- cover for src/chevt6.c.
 *
 * The chapter 29 handler at 000395d0 is the family's inline range merge with a
 * side gate bolted in front of it, and the two halves are covered separately.
 *
 * The merge half is covered the way the chapter 15 and 16 handlers are: its two
 * bounds, the signed inclusive compare between them and the 0xf0 mask are all
 * literals in its instruction stream -- MOV dword ptr [EBP-0x24],0x24 at
 * 000395fb, MOV dword ptr [EBP-0x20],0x59 at 00039602, CMP EAX,[EBP-0x14] / JLE
 * at 0003962b, and AND DL,0xf0 at 0003964b -- and the absence of a guard in
 * front of the loop is asserted by putting the shared one-shot latch slot up and
 * watching it run anyway.
 *
 * The gate half is what this handler has and its siblings do not: MOV AL,byte
 * ptr [EAX+0x6] / AND EAX,0xff / CMP EAX,0x2 / JNZ at 000395ee..000395f9, an
 * unsigned equality on the side byte of the record the argument names.  It is
 * covered by driving every side value through it and by moving which record the
 * argument names, because this is the only handler of the family whose incoming
 * index reaches anything -- the others overwrite the slot with 0 on entry.
 *
 * The unit array is staged here rather than read from a game file, because the
 * handler takes its whole effect through data_fdps_map_unit_array_ptr --
 * pointing that global at a local block is the only way to see the stores, and
 * it is also the only way to give the argument a record to be gated on.  What
 * the global itself holds is ticket 23's and is not asserted, so every case
 * writes the state it wants to see changed.
 *
 * Which indices the range means comes from map28.dat: byte 1 declares 12 player
 * slots and byte 2 declares 80 scenario units, so unit indices 0x00..0x0b are
 * the party and deployment records 0..79 follow at 0x0c..0x5b, and 0x24..0x59
 * is records 24..77.  Records 24..59 carry behaviour code 2 at deployment record
 * offset 0x11 and records 60..77 carry 0.  The top bound is pinned hardest here
 * for a reason the sibling handlers do not have: records 78 and 79, the two
 * level 30 Guardian Dragons, sit at unit indices 0x5a and 0x5b and carry
 * behaviour 2 as well, so a loop written with <= 0x5b, or one that ran to the
 * end of the array, would release the chapter's two boss units with the ambush.
 */
#include <stddef.h>
#include "testharn.h"
#include "fdpstype.h"
#include "gamedata.h"
#include "unit.h"
#include "chevt6.h"

/* The single inclusive range the one inline loop covers, read off the constants
   at 000395fb (0x24) and 00039602 (0x59) with the signed JLE at 0003962e. */
#define CH29_FIRST_INDEX 0x24
#define CH29_LAST_INDEX  0x59

/* How many records that range covers: 0x59 - 0x24 + 1. */
#define CH29_RANGE_UNITS 54

/* map28.dat's byte 1: the party occupies unit indices 0x00..0x0b, so the first
   deployment record is unit index 0x0c. */
#define CH29_FIRST_ENEMY_INDEX 0x0c

/* Unit index of deployment record 78, the first of the two level 30 Guardian
   Dragons.  It is one past the top of the range and holds behaviour 2, so it is
   the record an inclusive-bound mistake at the top would release. */
#define CH29_GUARDIAN_DRAGON_INDEX 0x5a

/* Unit index of deployment record 79, the second Guardian Dragon and the last
   unit map28.dat deploys: 0x0c + 79. */
#define CH29_LAST_DEPLOYED_INDEX 0x5b

/* Enough records to hold that whole deployment and four past it, so an
   off-by-one at either end of the range has somewhere visible to land. */
#define CH29_STAGE_UNITS 0x60

/* The three side codes: 2 is the player, 0 the enemy and 1 the neutral side. */
#define CH29_PLAYER_SIDE  2
#define CH29_ENEMY_SIDE   0
#define CH29_NEUTRAL_SIDE 1

/* The slot of data_fdps_map_cell_event_triggered_flags the one-shot handlers of
   this family latch -- element 0x10, the first the map's own event codes cannot
   reach.  This handler does not use it, and that is what is asserted. */
#define CH29_LATCH_SLOT 0x10

static struct fdps_unit_record ch29_units[CH29_STAGE_UNITS];

/* Give every record the same AI byte, lay the sides out the way map28.dat does
   -- the party on the player side at 0x00..0x0b and every deployment record on
   the enemy side above it -- and point the array global at the block.  The
   staged AI value carries a high nibble as well as a behaviour code, because the
   whole point of the merge is that only one of the two moves. */
static void stage_ch29_units(int ai_behavior)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        ch29_units[i].ai_behavior = (unsigned char) ai_behavior;
        if (i < CH29_FIRST_ENEMY_INDEX) {
            ch29_units[i].side = CH29_PLAYER_SIDE;
        } else {
            ch29_units[i].side = CH29_ENEMY_SIDE;
        }
    }
    data_fdps_map_unit_array_ptr = (unsigned char *) ch29_units;
}

/* The record has to be 0x50 bytes with its side byte at +6 and its AI byte at
   +0x34 for the emitted C to address the bytes the MOV AL,[EAX+0x6] load at
   000395ee and the MOV [EAX+0x34],DH store at 00039656 address; the stride is
   the IMUL 0x50 inside fdps_get_unit_record. */
static void ch29_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), 0x50);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, side), 6);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, ai_behavior), 0x34);
}

/* Fired for a player unit, exactly indices 0x24..0x59 are rewritten and every
   record either side of the range is left as it was.  The staged 0x52 is
   behaviour code 2 -- hold position, which is what byte 0x11 of map28.dat's
   deployment records gives records 24..59 and 78..79 -- under a high nibble of
   0x50; the range comes out 0x50 because the mode ORed in is 0, and the rest
   keep 0x52.  Indices 0..0x23 cover the twelve party slots and deployment
   records 0..23, and 0x5a..0x5f run from the two Guardian Dragons past the end
   of the deployment: both ends have to be untouched. */
static void ch29_player_unit_clears_exactly_the_range(void)
{
    int i;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (i >= CH29_FIRST_INDEX && i <= CH29_LAST_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x50);
        } else {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        }
    }
}

/* The range covers 54 records, not 53: 0x59 - 0x24 + 1, and the top bound is
   inclusive because the compare at 0003962b is JLE.  The count is asserted by
   counting the records that moved, and both boundary records are named on their
   own -- 0x59 must move and 0x5a must not -- because a rewrite of the loop as
   i < 0x59 is exactly the mistake that leaves map28.dat's deployment record 77
   guarding its tile and changes nothing else. */
static void ch29_range_includes_the_last_index(void)
{
    int i;
    int moved;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    moved = 0;
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (ch29_units[i].ai_behavior == 0x50) {
            moved++;
        }
    }
    CHECK_EQ(moved, CH29_RANGE_UNITS);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX + 1].ai_behavior, 0x52);
}

/* The two Guardian Dragons sit immediately above the top bound and hold the same
   behaviour code as the units that are released, so nothing but the bound keeps
   them in place.  Both are asserted unchanged, along with the first record below
   the bottom bound, which is deployment record 23 and holds position for the
   same reason. */
static void ch29_leaves_the_guardian_dragons_holding(void)
{
    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
}

/* The gate is an equality on 2 and nothing else reaches the loop.  The side byte
   is zero-extended by AND EAX,0xff at 000395f1 before the compare, so it is the
   whole byte that is tested: the enemy side 0, the neutral side 1, and the
   values 3 and 0xff that no deployment record carries all have to leave the map
   alone.  Each case is checked at both ends of the range, which is the pair a
   partially-run loop would show up in. */
static void ch29_only_the_player_side_springs_the_ambush(void)
{
    static int blocked_sides[4] = {CH29_ENEMY_SIDE, CH29_NEUTRAL_SIDE, 3, 0xff};
    int i;

    for (i = 0; i < 4; i++) {
        stage_ch29_units(0x52);
        ch29_units[0].side = (unsigned char) blocked_sides[i];
        fdps_chapter_29_event_activate_enemy_groups(0);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
}

/* The gate reads the record the argument names, not a fixed one, so which unit
   walked over the tile is what decides.  With map28.dat's own side layout staged
   -- the party at 0x00..0x0b and every deployment record on the enemy side above
   it -- each of the twelve party indices springs the ambush and a spread of
   enemy indices, including ones inside the released range and the last deployed
   unit, does not.  A unit inside the range that is on the player side is not
   special-cased either: it springs the ambush and is itself rewritten, because
   the loop covers its index like any other. */
static void ch29_gate_follows_the_argument_unit(void)
{
    static int enemy_indices[5] = {CH29_FIRST_ENEMY_INDEX, 0x24, 0x40, 0x59,
                                   CH29_LAST_DEPLOYED_INDEX};
    int i;

    for (i = 0; i < CH29_FIRST_ENEMY_INDEX; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_enemy_groups(i);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
    }

    for (i = 0; i < 5; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_enemy_groups(enemy_indices[i]);
        CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0x30].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_enemy_groups(0x30);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
}

/* The high nibble is carried across untouched by the AND 0xf0 at 0003964b and
   the low nibble ends at 0 whatever it held.  0x40 and 0x80 are the two AI flags
   read elsewhere -- fdps_map_actor_take_best_action and
   fdps_score_targets_for_item -- so a merge that assigned the mode whole, or
   that masked with anything wider, would drop them.  Expected values are the
   staged byte ANDed with 0xf0. */
static void ch29_keeps_the_high_nibble(void)
{
    stage_ch29_units(0);
    ch29_units[0x24].ai_behavior = 0xc2;
    ch29_units[0x25].ai_behavior = 0x02;
    ch29_units[0x30].ai_behavior = 0xff;
    ch29_units[0x44].ai_behavior = 0x40;
    ch29_units[0x59].ai_behavior = 0x8b;

    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(ch29_units[0x24].ai_behavior, 0xc0);
    CHECK_EQ(ch29_units[0x25].ai_behavior, 0x00);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0xf0);
    CHECK_EQ(ch29_units[0x44].ai_behavior, 0x40);
    CHECK_EQ(ch29_units[0x59].ai_behavior, 0x80);
}

/* One byte of one record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 first -- which also happens to make the acting
   unit's side byte 0x55, so the gate is opened by setting that one byte to 2 and
   nothing else is disturbed.  A store that landed at +0x33 or +0x35 -- the
   death-script operand's high byte and ai_dest_x -- is visible against 0x55, and
   0x55's low nibble is not already 0, so the write that should happen is visible
   too.  Both ends of the range are checked, because the stride the store is
   indexed by is the IMUL 0x50 inside fdps_get_unit_record and an error in it
   shows up furthest from the base. */
static void ch29_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0x55;
    }
    bytes[6] = CH29_PLAYER_SIDE;
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_29_event_activate_enemy_groups(0);

    CHECK_EQ(bytes[0x24 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x24 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x24 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x23 * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x5a * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[6], CH29_PLAYER_SIDE);
}

/* Nothing guards the loop: the instruction after the gate's JNZ at 000395f9 is
   the first of the three constant stores, with no compare between them, so this
   handler has no one-shot latch and runs its loop every time it is called with a
   player unit.  The latch slot is put up before the call and the range still
   moves; the slot is also asserted unchanged, because a handler that had grown a
   latch would have written it.  A second call is made too: the merge is
   idempotent, so it must leave the same values. */
static void ch29_has_no_one_shot_latch(void)
{
    stage_ch29_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);

    fdps_chapter_29_event_activate_enemy_groups(0);
    CHECK_EQ(ch29_units[CH29_FIRST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX + 1].ai_behavior, 0x52);
}

void run_chevt6_tests(void)
{
    RUN_TEST(ch29_record_shape_matches_the_offsets);
    RUN_TEST(ch29_player_unit_clears_exactly_the_range);
    RUN_TEST(ch29_range_includes_the_last_index);
    RUN_TEST(ch29_leaves_the_guardian_dragons_holding);
    RUN_TEST(ch29_only_the_player_side_springs_the_ambush);
    RUN_TEST(ch29_gate_follows_the_argument_unit);
    RUN_TEST(ch29_keeps_the_high_nibble);
    RUN_TEST(ch29_touches_no_neighbouring_byte);
    RUN_TEST(ch29_has_no_one_shot_latch);
}
