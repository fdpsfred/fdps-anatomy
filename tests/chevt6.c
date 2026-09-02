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
 *
 * The chapter 29 handler at 00039670 is the same gate in front of two of those
 * merges instead of one, so the ch29all_ cases cover the same ground once more
 * -- both bounds of both ranges, both 0xf0 masks, the gate and the absent latch
 * -- and add the one thing the single-range handler cannot show: which of the
 * two values a record ends up with.  The ranges are 0x0c..0x59 with mode 0 and
 * 0x5a..0x5b with mode 0x0a, so together they are every one of map28.dat's 80
 * deployment records, and the two Guardian Dragons at 0x5a and 0x5b are the
 * pair that must come out at 0x0a rather than at the 0 everything below them
 * gets.
 *
 * The chapter 30 handler at 000398c0 shares none of that ground: its whole body
 * is one deployment call, so the ch30w3_ cases run that deployment for real and
 * read back which record landed on which tile.  They need ICON.CEL and FIELD.VFS
 * next to the executable and skip themselves without them.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

/* How many records the all-enemies handler's first range covers: 0x59 - 0x0c +
   1, map28.dat's deployment records 0..77.  Records 78 and 79 are the second
   range's and are not in this count. */
#define CH29ALL_ADVANCE_UNITS 78

/* The behaviour code the second range ORs in, read off MOV dword ptr
   [EBP-0x30],0xa at 0003970d, and the byte it produces over the staged high
   nibble 0x50. */
#define CH29ALL_DRAGON_MODE 0x0a
#define CH29ALL_DRAGON_BYTE 0x5a

/* The handler at 00039670 releases the whole deployment, and the two ranges
   land different values: unit indices 0x0c..0x59 come out with behaviour code 0
   under their own high nibble, 0x5a and 0x5b with 0x0a, the party at
   0x00..0x0b is outside both ranges, and so is everything above 0x5b.  Staged
   0x52 is behaviour code 2 -- hold position, which is what byte 0x11 of
   map28.dat's deployment records gives records 0..59 and 78..79 -- under a high
   nibble of 0x50, so the expected bytes are 0x50 and 0x5a. */
static void ch29all_releases_the_whole_deployment(void)
{
    int i;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_all_enemies(0);

    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (i < CH29_FIRST_ENEMY_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        } else if (i <= CH29_LAST_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x50);
        } else if (i <= CH29_LAST_DEPLOYED_INDEX) {
            CHECK_EQ(ch29_units[i].ai_behavior, CH29ALL_DRAGON_BYTE);
        } else {
            CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
        }
    }
}

/* Both compares are JLE, so both ranges are inclusive at the top: 0x59 is the
   last index the first loop writes and 0x5b the last the second writes.  The
   count of records that came out at 0x50 is 78 -- 0x59 - 0x0c + 1 -- and the
   four boundary records are named on their own, because i < 0x59 would leave
   deployment record 77 guarding its tile and i < 0x5b would leave one of the
   two Guardian Dragons in hold-position mode. */
static void ch29all_both_ranges_are_inclusive(void)
{
    int i;
    int advanced;

    stage_ch29_units(0x52);
    fdps_chapter_29_event_activate_all_enemies(0);

    advanced = 0;
    for (i = 0; i < CH29_STAGE_UNITS; i++) {
        if (ch29_units[i].ai_behavior == 0x50) {
            advanced++;
        }
    }
    CHECK_EQ(advanced, CH29ALL_ADVANCE_UNITS);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX - 1].ai_behavior, 0x52);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX + 1].ai_behavior, 0x52);
}

/* The two ranges are disjoint -- the first covers 0x0c..0x59 and the second
   0x5a..0x5b -- so the dragons are written by the second loop alone, and what
   they come out with is 0x0a and not the 0 their 78 neighbours take.  Guide chapter 29: the 魔龍 fires 火焰 for 300 direct damage down a
   14-tile line and 使用後還可再近身攻擊一次, still gets a melee attack after
   using it, which is mode 0x0a's item arm followed by its attack arm; mode 0
   would pick one action or the other and would also let the dragons cross the
   map.  The dragons are staged with a high nibble of their own, 0x30, so the
   assertion pins the byte to the second range's mode rather than to the value
   the first range's would have produced. */
static void ch29all_dragons_get_the_item_then_attack_mode(void)
{
    stage_ch29_units(0x52);
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior = 0x32;
    ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior = 0x32;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             0x30 | CH29ALL_DRAGON_MODE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             0x30 | CH29ALL_DRAGON_MODE);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
}

/* The gate is an equality on 2 and nothing else reaches either loop.  The side
   byte is zero-extended by AND EAX,0xff at 00039691 before the compare, so the
   whole byte is tested: the enemy side 0, the neutral side 1, and the values 3
   and 0xff that no deployment record carries all have to leave the map alone.
   Each case is checked at both ends of the first range and on a dragon, which
   is the trio a partially-run pair of loops would show up in. */
static void ch29all_only_the_player_side_springs_the_ambush(void)
{
    static int blocked_sides[4] = {CH29_ENEMY_SIDE, CH29_NEUTRAL_SIDE, 3, 0xff};
    int i;

    for (i = 0; i < 4; i++) {
        stage_ch29_units(0x52);
        ch29_units[0].side = (unsigned char) blocked_sides[i];
        fdps_chapter_29_event_activate_all_enemies(0);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[0].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
}

/* The gate reads the record the argument names, not a fixed one.  With
   map28.dat's own side layout staged, each of the twelve party indices springs
   the ambush -- and is itself left alone, because the party sits below the
   bottom bound -- while a spread of enemy indices inside both ranges does not.
   A unit inside the first range that is on the player side is not special-cased
   either: it springs the ambush and is rewritten like any other index, and the
   same goes for a dragon index. */
static void ch29all_gate_follows_the_argument_unit(void)
{
    static int enemy_indices[5] = {CH29_FIRST_ENEMY_INDEX, 0x40, CH29_LAST_INDEX,
                                   CH29_GUARDIAN_DRAGON_INDEX,
                                   CH29_LAST_DEPLOYED_INDEX};
    int i;

    for (i = 0; i < CH29_FIRST_ENEMY_INDEX; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_all_enemies(i);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
                 CH29ALL_DRAGON_BYTE);
        CHECK_EQ(ch29_units[i].ai_behavior, 0x52);
    }

    for (i = 0; i < 5; i++) {
        stage_ch29_units(0x52);
        fdps_chapter_29_event_activate_all_enemies(enemy_indices[i]);
        CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x52);
        CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x52);
    }

    stage_ch29_units(0x52);
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].side = CH29_PLAYER_SIDE;
    fdps_chapter_29_event_activate_all_enemies(CH29_GUARDIAN_DRAGON_INDEX);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
}

/* Both merges carry the high nibble across untouched -- AND DL,0xf0 at 000396ef
   and 0003974f -- and set the low nibble to 0 and 0x0a respectively whatever it
   held.  0x40 and 0x80 are the two AI flags read elsewhere, so a merge that
   assigned the mode whole would drop them.  Expected values are the staged byte
   ANDed with 0xf0, ORed with the mode of whichever range the index falls in. */
static void ch29all_keeps_the_high_nibble(void)
{
    stage_ch29_units(0);
    ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior = 0xc2;
    ch29_units[0x30].ai_behavior = 0xff;
    ch29_units[0x44].ai_behavior = 0x40;
    ch29_units[CH29_LAST_INDEX].ai_behavior = 0x8b;
    ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior = 0xc2;
    ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior = 0x0f;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0xc0);
    CHECK_EQ(ch29_units[0x30].ai_behavior, 0xf0);
    CHECK_EQ(ch29_units[0x44].ai_behavior, 0x40);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x80);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior, 0xca);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior, 0x0a);
}

/* One byte of each record moves and nothing either side of it does.  Every byte
   of the block is stamped 0x55 -- which makes the acting unit's side byte 0x55
   as well, so the gate is opened by setting that one byte to 2 and nothing else
   is disturbed.  A store that landed at +0x33 or +0x35 -- the death-script
   operand's high byte and ai_dest_x -- is visible against 0x55, and 0x55's low
   nibble is not already 0 or 0x0a, so the writes that should happen are visible
   too.  Both ends of each range are checked, because the stride the stores are
   indexed by is the IMUL 0x50 inside fdps_get_unit_record and an error in it
   shows up furthest from the base. */
static void ch29all_touches_no_neighbouring_byte(void)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) ch29_units;
    for (i = 0; i < (int) sizeof(ch29_units); i++) {
        bytes[i] = 0x55;
    }
    bytes[6] = CH29_PLAYER_SIDE;
    data_fdps_map_unit_array_ptr = bytes;

    fdps_chapter_29_event_activate_all_enemies(0);

    CHECK_EQ(bytes[0x0c * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x0c * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x0c * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x34], 0x50);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x59 * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x5a * 0x50 + 0x34], CH29ALL_DRAGON_BYTE);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x34], CH29ALL_DRAGON_BYTE);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x33], 0x55);
    CHECK_EQ(bytes[0x5b * 0x50 + 0x35], 0x55);
    CHECK_EQ(bytes[0x0b * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[0x5c * 0x50 + 0x34], 0x55);
    CHECK_EQ(bytes[6], CH29_PLAYER_SIDE);
}

/* Nothing guards either loop: the instruction after the gate's JNZ at 00039699
   is the first of the first expansion's constant stores, with no compare
   between them, so this handler has no one-shot latch and runs both loops every
   time it is called with a player unit.  The latch slot is put up before the
   call and the ranges still move; the slot is also asserted unchanged, because
   a handler that had grown a latch would have written it.  A second call is
   made too: both merges are idempotent, so it must leave the same values --
   including on the dragons, whose byte the first loop does not reach. */
static void ch29all_has_no_one_shot_latch(void)
{
    stage_ch29_units(0x52);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    fdps_chapter_29_event_activate_all_enemies(0);
    CHECK_EQ(ch29_units[CH29_FIRST_ENEMY_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_LAST_INDEX].ai_behavior, 0x50);
    CHECK_EQ(ch29_units[CH29_GUARDIAN_DRAGON_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX].ai_behavior,
             CH29ALL_DRAGON_BYTE);
    CHECK_EQ(ch29_units[CH29_LAST_DEPLOYED_INDEX + 1].ai_behavior, 0x52);
}

/* Chapter 30's "the second form has fallen" event at 000398c0, from here down.
 *
 * The handler's whole body is one call, and none of the three values it hands
 * fdps_deploy_wave -- PUSH dword ptr [0x00069cf4], PUSH 0x3 and a EAX holding 1,
 * at 000398d3..000398db -- is left anywhere afterwards.  So the only way to see
 * any of them is to let the deployment happen, and these cases run
 * fdps_deploy_wave for real, then read back what landed where:
 *
 *   MAP00.COD  record 0 (18, 0)  record 1 (22, 12)  record 2 (8, 10)
 *   MAP01.COD  record 0 (9, 4)
 *
 * -- the same records tests/deploy.c's own wave cases and the chapter 17 and 18
 * cases in tests/chevt3.c expect, staged through tests/gamefile.lst.  That
 * function opens ICON.CEL and FIELD.VFS for itself and a run without them would
 * not fail a check, it would hang in fdps_wait_any_key, so every case here skips
 * itself when they are not there.
 *
 * Which wave the spawn table's records carry is staged rather than read from
 * map29.dat, because what is under test is that the wave asked for is the
 * literal 3 and not something derived: a record is planted at each of the waves
 * either side of it, so asking for the wrong one is visible as a different
 * character id on a different tile.  map29.dat's own wave 3 is one record, the
 * boss's third form, and a fixture of one record could not tell "wave 3" from
 * "every record" or from "the first record".
 *
 * The unit array is malloc'd rather than staged into a static block, because a
 * deployment reallocs it: a static block handed to realloc is not a smaller
 * fixture, it is undefined behaviour.  It is never freed, for the same reason
 * tests/deploy.c does not free it -- the block moves under the global on every
 * deployment.
 */

/* The wave the handler asks for: PUSH 0x3 at 000398d9. */
#define CH30W3_WAVE_NO 3

/* A map big enough for MAP00.COD's records, which reach (22, 12).  The tile
   search a placement flag of 0 would run walks the whole grid, and the marking
   pass writes at a unit's own tile with no bound, so the grid has to cover the
   coordinates the real placement file names either way. */
#define CH30W3_GRID_W 32
#define CH30W3_GRID_H 16

/* Where the deployment records start in the MAP%02d.DAT block and where its
   header keeps their count: ADD EAX,0x83 at 00023463 and MOV AL,byte ptr
   [EAX+0x2] at 000238bb. */
#define CH30W3_SPAWN_TABLE_RECORD_BASE 0x83
#define CH30W3_SPAWN_TABLE_COUNT_OFFSET 2

/* The scene-layer offsets fdps_map_load_tile_info reads through: the tile map's
   width word at +7 with its 16-bit ids from +0xb, the attribute table's 4-byte
   rows from +0x11, and the event layer's width at +7 with its cells from +0x10
   (src/maptile.c). */
#define CH30W3_TILE_MAP_WIDTH_OFFSET 7
#define CH30W3_TILE_MAP_IDS_OFFSET 0xb
#define CH30W3_TILE_ATTR_ROWS_OFFSET 0x11
#define CH30W3_EVENT_LAYER_WIDTH_OFFSET 7
#define CH30W3_EVENT_LAYER_CELLS_OFFSET 0x10

/* A terrain code the deployment search accepts and one it rejects, either side
   of the CMP EAX,0x5 / JGE at 00023404. */
#define CH30W3_TERRAIN_WALKABLE 1
#define CH30W3_TERRAIN_BLOCKED 5

#define CH30W3_TILE_ATTR_ROWS 16
#define CH30W3_CHAR_TABLE_ROWS 8
#define CH30W3_ENEMY_TABLE_ROWS 8

/* Every item id is a valid index into the staged item table, 0xff included:
   fdps_unit_recompute_combat_stats follows an equipped flag into
   fdps_get_item_record without a bounds check. */
#define CH30W3_ITEM_TABLE_ROWS 256
#define CH30W3_ITEM_ID_NONE 0xff

/* The unit array's stride, as the deployment path allocates it: PUSH 0x50 at
   000232cc. */
#define CH30W3_UNIT_STRIDE 0x50

/* The three staged deployment records and the wave each is tagged with, one at
   the wave the handler must ask for and one at each of its neighbours.  The
   character ids are arbitrary and only have to differ, so that which record was
   deployed is readable off the unit as well as off the tile. */
#define CH30W3_WAVE2_RECORD 0
#define CH30W3_WAVE3_RECORD 1
#define CH30W3_WAVE4_RECORD 2
#define CH30W3_WAVE2_CHAR_ID 5
#define CH30W3_WAVE3_CHAR_ID 6
#define CH30W3_WAVE4_CHAR_ID 7

static unsigned char ch30w3_grid[4 + CH30W3_GRID_W * CH30W3_GRID_H * 2];
static unsigned char ch30w3_spawn_table[CH30W3_SPAWN_TABLE_RECORD_BASE +
                                        8 * 0x1a];
static unsigned char ch30w3_tile_map[CH30W3_TILE_MAP_IDS_OFFSET +
                                     CH30W3_GRID_W * CH30W3_GRID_H * 2];
static unsigned char ch30w3_tile_attr[CH30W3_TILE_ATTR_ROWS_OFFSET +
                                      CH30W3_TILE_ATTR_ROWS * 4];
static unsigned char ch30w3_event_layer[CH30W3_EVENT_LAYER_CELLS_OFFSET +
                                        CH30W3_GRID_W * CH30W3_GRID_H];
static struct fdps_character_base_record ch30w3_char_base[
    CH30W3_CHAR_TABLE_ROWS];
static struct fdps_character_growth ch30w3_growth[CH30W3_CHAR_TABLE_ROWS];
static struct fdps_enemy_data ch30w3_enemy[CH30W3_ENEMY_TABLE_ROWS];
static struct fdps_item_effect ch30w3_items[CH30W3_ITEM_TABLE_ROWS];

static int ch30w3_files_checked = 0;
static int ch30w3_files_ready = 0;

/* ICON.CEL is read by fdps_cache_cel_sprite_group, which takes a fixed
   0x2970-byte bite out of the sheet's offset table, so a shorter file is one
   that reader runs off the end of rather than a smaller fixture.  FIELD.VFS
   cannot be stood in for at all: a missing container sends fdps_deploy_wave into
   fdps_wait_any_key and a missing member into exit(1). */
static void ch30w3_ensure_game_files(void)
{
    FILE *fp;
    long size;

    if (ch30w3_files_checked) {
        return;
    }
    ch30w3_files_checked = 1;

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
    ch30w3_files_ready = 1;
}

static void ch30w3_zero_bytes(void *block, int count)
{
    unsigned char *bytes;
    int i;

    bytes = (unsigned char *) block;
    for (i = 0; i < count; i++) {
        bytes[i] = 0;
    }
}

static struct fdps_char_spawn_record *ch30w3_spawn_at(int index)
{
    return (struct fdps_char_spawn_record *)
           (ch30w3_spawn_table + CH30W3_SPAWN_TABLE_RECORD_BASE) + index;
}

/* One deployment record with nothing equipped and nothing carried, tagged with
   the wave it belongs to. */
static void ch30w3_set_spawn(int index, int char_id, int wave_no)
{
    ch30w3_spawn_at(index)->char_id = (unsigned char) char_id;
    ch30w3_spawn_at(index)->level = 1;
    ch30w3_spawn_at(index)->side = 0;
    ch30w3_spawn_at(index)->equipped_item_0 = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->equipped_item_1 = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[0] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[1] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[2] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[3] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[4] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->carried_items[5] = CH30W3_ITEM_ID_NONE;
    ch30w3_spawn_at(index)->wave_no = (unsigned char) wave_no;
}

static void ch30w3_set_terrain(int tile_id, int terrain)
{
    struct fdps_tile_attr_entry *rows;

    rows = (struct fdps_tile_attr_entry *)
           (ch30w3_tile_attr + CH30W3_TILE_ATTR_ROWS_OFFSET);
    rows[tile_id].terrain_type = (unsigned char) terrain;
}

/* The cell of the tile map at (tile_x, tile_y), so one tile can be given an id
   whose attribute row says a search must not use it. */
static void ch30w3_set_tile_id(int tile_x, int tile_y, int tile_id)
{
    short *tile_ids;

    tile_ids = (short *) (ch30w3_tile_map + CH30W3_TILE_MAP_IDS_OFFSET);
    tile_ids[tile_y * CH30W3_GRID_W + tile_x] = (short) tile_id;
}

static struct fdps_unit_record *ch30w3_unit(int unit_index)
{
    return (struct fdps_unit_record *)
           (data_fdps_map_unit_array_ptr + unit_index * CH30W3_UNIT_STRIDE);
}

/* A blank walkable map with one unit already on it, the chapter global on map 0
   and the turn counter on the given turn, plus three deployment records tagged
   waves 2, 3 and 4 at table indices 0, 1 and 2.  Placement record and table
   index are the same number, so which record was deployed is readable twice
   over: off the character id and off the tile it landed on.

   The unit already on the map stands at (0, 0), clear of every placement record
   these cases read back. */
static void ch30w3_stage(int battle_turn)
{
    int i;

    ch30w3_zero_bytes(ch30w3_grid, (int) sizeof(ch30w3_grid));
    ch30w3_zero_bytes(ch30w3_spawn_table, (int) sizeof(ch30w3_spawn_table));
    ch30w3_zero_bytes(ch30w3_tile_map, (int) sizeof(ch30w3_tile_map));
    ch30w3_zero_bytes(ch30w3_tile_attr, (int) sizeof(ch30w3_tile_attr));
    ch30w3_zero_bytes(ch30w3_event_layer, (int) sizeof(ch30w3_event_layer));
    ch30w3_zero_bytes(ch30w3_char_base, (int) sizeof(ch30w3_char_base));
    ch30w3_zero_bytes(ch30w3_growth, (int) sizeof(ch30w3_growth));
    ch30w3_zero_bytes(ch30w3_enemy, (int) sizeof(ch30w3_enemy));
    ch30w3_zero_bytes(ch30w3_items, (int) sizeof(ch30w3_items));

    *(short *) ch30w3_grid = (short) CH30W3_GRID_W;
    *(short *) (ch30w3_grid + 2) = (short) CH30W3_GRID_H;

    *(short *) (ch30w3_tile_map + CH30W3_TILE_MAP_WIDTH_OFFSET) =
        (short) CH30W3_GRID_W;
    for (i = 0; i < CH30W3_TILE_ATTR_ROWS; i++) {
        ch30w3_set_terrain(i, CH30W3_TERRAIN_WALKABLE);
    }

    *(short *) (ch30w3_event_layer + CH30W3_EVENT_LAYER_WIDTH_OFFSET) =
        (short) CH30W3_GRID_W;

    data_fdps_battle_move_grid_ptr = ch30w3_grid;
    data_fdps_tile_event_data_table_ptr = ch30w3_spawn_table;
    data_fdps_scene_layer_tile_map_ptrs[0] = ch30w3_tile_map;
    data_fdps_scene_layer_tile_attr_ptr[0] = ch30w3_tile_attr;
    data_fdps_map_cell_event_code_layer_ptr = ch30w3_event_layer;
    data_fdps_battle_character_base_table_ptr =
        (unsigned char *) ch30w3_char_base;
    data_fdps_battle_character_growth_table_ptr =
        (unsigned char *) ch30w3_growth;
    data_fdps_battle_enemy_data_table_ptr = (unsigned char *) ch30w3_enemy;
    data_fdps_item_effect_table_ptr = (unsigned char *) ch30w3_items;

    data_fdps_map_unit_array_ptr = (unsigned char *) malloc(CH30W3_UNIT_STRIDE);
    ch30w3_zero_bytes(data_fdps_map_unit_array_ptr, CH30W3_UNIT_STRIDE);
    data_fdps_map_unit_count = 1;

    ch30w3_spawn_table[CH30W3_SPAWN_TABLE_COUNT_OFFSET] = 3;
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID, 2);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID, 3);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID, 4);

    data_fdps_chapter_current_chapter_id = 0;
    data_fdps_battle_turn_counter = battle_turn;
}

/* Move the wave-3 tag down onto table index 0, so the record the handler
   deploys is placement record 0 -- the one record of MAP01.COD these cases know
   the coordinates of. */
static void ch30w3_tag_first_record_as_the_wave(void)
{
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID,
                     CH30W3_WAVE_NO);
    ch30w3_set_spawn(CH30W3_WAVE3_RECORD, CH30W3_WAVE3_CHAR_ID, 2);
}

/* The fields the cases below read back and the stride they are indexed by.
   Every one of them would agree with itself while addressing another byte if the
   layout were wrong. */
static void ch30w3_record_shape_matches_the_offsets(void)
{
    CHECK_EQ((int) sizeof(struct fdps_unit_record), CH30W3_UNIT_STRIDE);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_x), 0);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, pos_y), 1);
    CHECK_EQ((int) offsetof(struct fdps_unit_record, char_id), 8);
    CHECK_EQ((int) offsetof(struct fdps_char_spawn_record, wave_no), 0x15);
}

/* The wave asked for is 3 and only the record tagged with it arrives: of the
   three staged records exactly one deploys, it is table index 1, and it lands on
   MAP00.COD record 1 at (22, 12) with the character id that record carries.  The
   records at waves 2 and 4 sit either side of it, so an off-by-one in either
   direction would put a different character on a different tile. */
static void ch30w3_deploys_the_wave_three_record(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
}

/* The 3 is a literal and not the battle turn counter, which is what every
   turn-scheduled handler of this family pushes instead: the counter is driven
   through the wave numbers the fixture carries and one well above them, and the
   same wave-3 record arrives every time.  A handler that read the counter would
   deploy the wave-2 record on turn 2, the wave-4 record on turn 4 and nothing at
   all on turn 0 or turn 11. */
static void ch30w3_wave_number_is_a_literal(void)
{
    static int turns[4] = {0, 2, 4, 11};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 4; i++) {
        ch30w3_stage(turns[i]);
        fdps_chapter_30_event_deploy_wave_3(0);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
    }
}

/* It is the whole wave and not one unit: map29.dat tags a single record wave 3,
   so nothing in the shipped data would tell a wave deploy from a single-unit
   one, and the fixture tags all three records instead.  All three arrive, in
   table order, on their own placement records -- (18, 0), (22, 12) and
   (8, 10) -- and the count goes up by three. */
static void ch30w3_deploys_every_record_of_the_wave(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_set_spawn(CH30W3_WAVE2_RECORD, CH30W3_WAVE2_CHAR_ID,
                     CH30W3_WAVE_NO);
    ch30w3_set_spawn(CH30W3_WAVE4_RECORD, CH30W3_WAVE4_CHAR_ID,
                     CH30W3_WAVE_NO);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 4);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 0);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, 12);
    CHECK_EQ((int) ch30w3_unit(3)->char_id, CH30W3_WAVE4_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(3)->pos_x, 8);
    CHECK_EQ((int) ch30w3_unit(3)->pos_y, 10);
}

/* The placement flag is 1 -- MOV EAX,0x1 / PUSH EAX at 000398d3 -- so the unit
   is put on the tile its placement record names and the terrain there is never
   consulted.  MAP00.COD record 1 names (22, 12); giving that one cell a tile id
   whose attribute row is terrain 5 is what takes a tile out of the search a flag
   of 0 runs, and the unit still lands on (22, 12).  The chapter 17 and 18
   handlers push 0 and their cases in tests/chevt3.c assert the same record
   landing on (22, 13) off the same fixture, so this is the assertion that
   separates the two flags. */
static void ch30w3_places_on_the_scripted_tile(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_set_tile_id(22, 12, 1);
    ch30w3_set_terrain(1, CH30W3_TERRAIN_BLOCKED);

    fdps_chapter_30_event_deploy_wave_3(0);

    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
}

/* The map the wave is deployed under is read from
   data_fdps_chapter_current_chapter_id at the call site and is not a literal:
   the wave-3 record is moved onto table index 0 and deployed twice, once with
   that global on 0 and once on 1, and it lands on MAP00.COD's record 0 at
   (18, 0) and then on MAP01.COD's record 0 at (9, 4). */
static void ch30w3_map_number_comes_from_the_chapter_global(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    ch30w3_tag_first_record_as_the_wave();

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 18);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 0);

    ch30w3_stage(0);
    ch30w3_tag_first_record_as_the_wave();
    data_fdps_chapter_current_chapter_id = 1;

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE2_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(1)->pos_x, 9);
    CHECK_EQ((int) ch30w3_unit(1)->pos_y, 4);
}

/* The incoming argument slot is overwritten with 0 at 000398cc before anything
   else happens and never read back, so the index the dispatcher passes cannot
   reach the wave asked for, the map asked for or the placement flag.  In the
   shipped data this handler is reached from fdps_run_death_scripts, which passes
   the index of the unit that just died; the values put through here are the 0
   the turn-event runner would pass, an index that names the unit already on the
   map, one past the array, and -1 and 30000, which are the ones an
   argument-driven handler would betray itself on. */
static void ch30w3_ignores_the_unit_index_argument(void)
{
    static int arguments[5] = {0, 1, 2, -1, 30000};
    int i;

    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    for (i = 0; i < 5; i++) {
        ch30w3_stage(0);
        fdps_chapter_30_event_deploy_wave_3(arguments[i]);
        CHECK_EQ(data_fdps_map_unit_count, 2);
        CHECK_EQ((int) ch30w3_unit(1)->char_id, CH30W3_WAVE3_CHAR_ID);
        CHECK_EQ((int) ch30w3_unit(1)->pos_x, 22);
        CHECK_EQ((int) ch30w3_unit(1)->pos_y, 12);
    }
}

/* Nothing guards the call: there is no compare anywhere in the body and no latch
   is written, so a second firing appends a second copy of the wave rather than
   being refused.  The slot the one-shot handlers of this family latch is put up
   beforehand and the wave still arrives, and the slot is asserted unchanged
   because a handler that had grown a latch would have written it. */
static void ch30w3_has_no_one_shot_latch(void)
{
    ch30w3_ensure_game_files();
    if (!ch30w3_files_ready) {
        return;
    }

    ch30w3_stage(0);
    data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT] = 1;

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 2);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);

    fdps_chapter_30_event_deploy_wave_3(0);
    CHECK_EQ(data_fdps_map_unit_count, 3);
    CHECK_EQ((int) ch30w3_unit(2)->char_id, CH30W3_WAVE3_CHAR_ID);
    CHECK_EQ((int) ch30w3_unit(2)->pos_x, 22);
    CHECK_EQ((int) ch30w3_unit(2)->pos_y, 12);
    CHECK_EQ(data_fdps_map_cell_event_triggered_flags[CH29_LATCH_SLOT], 1);
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
    RUN_TEST(ch29all_releases_the_whole_deployment);
    RUN_TEST(ch29all_both_ranges_are_inclusive);
    RUN_TEST(ch29all_dragons_get_the_item_then_attack_mode);
    RUN_TEST(ch29all_only_the_player_side_springs_the_ambush);
    RUN_TEST(ch29all_gate_follows_the_argument_unit);
    RUN_TEST(ch29all_keeps_the_high_nibble);
    RUN_TEST(ch29all_touches_no_neighbouring_byte);
    RUN_TEST(ch29all_has_no_one_shot_latch);
    RUN_TEST(ch30w3_record_shape_matches_the_offsets);
    RUN_TEST(ch30w3_deploys_the_wave_three_record);
    RUN_TEST(ch30w3_wave_number_is_a_literal);
    RUN_TEST(ch30w3_deploys_every_record_of_the_wave);
    RUN_TEST(ch30w3_places_on_the_scripted_tile);
    RUN_TEST(ch30w3_map_number_comes_from_the_chapter_global);
    RUN_TEST(ch30w3_ignores_the_unit_index_argument);
    RUN_TEST(ch30w3_has_no_one_shot_latch);
}
